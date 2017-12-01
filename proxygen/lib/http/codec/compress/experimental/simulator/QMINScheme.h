#pragma once

#include <assert.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionScheme.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKQueue.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>

#include "qmin_common.h"
#include "qmin_dec.h"
#include "qmin_enc.h"

namespace proxygen {  namespace compress {
class QMINScheme : public CompressionScheme {
 public:
  explicit QMINScheme(CompressionSimulator* sim)
      : CompressionScheme(sim)
  {
    qms_ctl[0].out.qco_write = write_enc2dec;
    qms_ctl[1].out.qco_write = write_dec2enc;

    qms_enc = qmin_enc_new(QSIDE_CLIENT, 64 * 1024, &qms_ctl[0].out);
    qms_dec = qmin_dec_new(QSIDE_SERVER, 64 * 1024, &qms_ctl[1].out);

    qms_next_stream_id_to_encode = 1;
  }

  ~QMINScheme()
  {
    qmin_enc_destroy(qms_enc);
    qmin_dec_destroy(qms_dec);
  }

  /* QMIN Ack carries QMM_STREAM_DONE and QMM_ACK_FLUSH messages from decoder
   * to the encoder.
   */
  struct QMINAck : public CompressionScheme::Ack {
    explicit QMINAck (const void *buf, size_t bufsz)
    {
      qma_sz = bufsz;
      memcpy(qma_buf, buf, bufsz);
    }

    size_t        qma_sz;
    unsigned char qma_buf[0x1000];
  };

  std::unique_ptr<Ack> getAck(uint16_t seqn) override
  {
    if (qms_ctl[1].off)
    {
      auto ack = std::make_unique<QMINAck>(qms_ctl[1].buf, qms_ctl[1].off);
      qms_ctl[1].off = 0;
      return ack;
    }
    else
    {
      assert(0);
      return nullptr;
    }
  }

  void recvAck(std::unique_ptr<Ack> generic_ack) override
  {
    CHECK(generic_ack);
    auto ack = dynamic_cast<QMINAck*>(generic_ack.get());
    CHECK_NOTNULL(ack);

    ssize_t nread;
    nread = qmin_enc_cmds_in(qms_enc, ack->qma_buf, ack->qma_sz);
    if (nread < 0 || (size_t) nread != ack->qma_sz)
      VLOG(1) << "error: qmin_enc_cmds_in failed";
  }

  std::pair<bool, std::unique_ptr<folly::IOBuf>> encode(
    std::vector<compress::Header> allHeaders, SimStats& stats) override
  {
    const size_t max_ctl = 0x1000;
    const size_t max_comp = 0x1000;
    unsigned char outbuf[max_ctl + max_comp];
    unsigned char *const comp = outbuf + max_ctl;
    size_t nw, comp_sz;
    enum qmin_encode_status qes;

    qms_ctl[0].off = 0;
    qms_ctl[0].out.qco_ctx = this;
    comp_sz = 0;

    for (const auto header : allHeaders) {
      std::string name{header.name->c_str()};
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      qes = qmin_enc_encode(qms_enc, qms_next_stream_id_to_encode, name.c_str(),
        name.length(), header.value->c_str(), header.value->length(), QIT_YES,
        comp + comp_sz, max_comp - comp_sz, &nw);
      switch (qes)
      {
      case QES_OK:
        stats.uncompressed += name.length() + header.value->length();
        stats.compressed += nw;
        comp_sz += nw;
        break;
      case QES_NOBUFS:
        VLOG(1) << "compressed header does not fit into temporary output buffer";
        return {false, nullptr};
      case QES_ERR:
        VLOG(1) << "error: " << strerror(errno);
        return {false, nullptr};
      }
    }

    if (0 != qmin_enc_end_stream_headers(qms_enc))
      VLOG(1) << "error: qmin_enc_end_stream_headers failed";

    /* Prepend control message and its size: */
    if (qms_ctl[0].off)
      memcpy(outbuf + max_ctl - qms_ctl[0].off, qms_ctl[0].buf, qms_ctl[0].off);
    memcpy(outbuf + max_ctl - qms_ctl[0].off - sizeof(qms_ctl[0].off), &qms_ctl[0].off,
           sizeof(qms_ctl[0].off));
    stats.compressed += sizeof(qms_ctl[0].off) + qms_ctl[0].off;

    /* Prepend Stream ID: */
    memcpy(outbuf + max_ctl - qms_ctl[0].off - sizeof(qms_ctl[0].off) - sizeof(uint32_t),
           &qms_next_stream_id_to_encode, sizeof(qms_next_stream_id_to_encode));

    qms_next_stream_id_to_encode += 2;
    return {true, folly::IOBuf::copyBuffer(outbuf + max_ctl - qms_ctl[0].off -
      sizeof(qms_ctl[0].off) - sizeof(uint32_t),
      comp_sz + qms_ctl[0].off + sizeof(qms_ctl[0].off) + sizeof(uint32_t))};
  }

  void decode(bool, std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats&, SimStreamingCallback& callback) override
  {
    folly::io::Cursor cursor(encodedReq.get());
    const unsigned char *buf;
    ssize_t nread;
    size_t ctl_sz;
    char outbuf[0x1000];
    unsigned name_len, val_len;
    unsigned decoded_size = 0;
    uint32_t stream_id;

    qms_ctl[1].off = 0;
    qms_ctl[1].out.qco_ctx = this;

    /* Read Stream ID: */
    buf = cursor.data();
    memcpy(&stream_id, buf, sizeof(uint32_t));
    encodedReq->trimStart(sizeof(uint32_t));

    /* Read size of control messages */
    buf = cursor.data();
    memcpy(&ctl_sz, buf, sizeof(ctl_sz));
    encodedReq->trimStart(sizeof(ctl_sz));

    /* Feed control messages to the decoder: */
    if (ctl_sz)
    {
      buf = cursor.data();
      nread = qmin_dec_cmds_in(qms_dec, buf, ctl_sz);
      if (nread < 0 || (size_t) nread != ctl_sz)
      {
        VLOG(1) << "oops: could not get all commands in";
        return;
      }
      encodedReq->trimStart(ctl_sz);
    }

    buf = cursor.data();
    const unsigned char *const end = buf + cursor.length();

    while (buf < end)
    {
      nread = qmin_dec_decode(qms_dec, buf, end - buf, outbuf, sizeof(outbuf),
                              &name_len, &val_len);
      if (nread < 0)
      {
        VLOG(1) << "ERROR: decoder failed!";
        return;
      }
      assert(nread);
      buf += nread;
      decoded_size += name_len + val_len;
      std::string name{outbuf, name_len};
      std::string value{outbuf + name_len, val_len};
      callback.onHeader(name, value);
    }

    if (0 != qmin_dec_stream_done(qms_dec, stream_id))
      VLOG(1) << "error: qmin_dec_stream_done failed";

    callback.onHeadersComplete(proxygen::HTTPHeaderSize{decoded_size});
  }

  uint32_t getHolBlockCount() const override
  {
    return 0;
  }

  void runLoopCallback() noexcept override
  {
    CompressionScheme::runLoopCallback();
  }

  void write_ctl_msg (const void *buf, size_t sz, unsigned idx)
  {
    size_t avail = sizeof(qms_ctl[idx].buf) - qms_ctl[idx].off;
    if (avail < sz)
    {
      VLOG(1) << "Truncating control message from " << sz << " to "
              << avail << "bytes";
      sz = avail;
    }
    memcpy(qms_ctl[idx].buf + qms_ctl[idx].off, buf, sz);
    qms_ctl[idx].off += sz;
    VLOG(4) << "Wrote " << sz << " bytes to control channel";
  }

  static void write_enc2dec (void *ctx, const void *buf, size_t sz)
  {
    QMINScheme *const qms = (QMINScheme *) ctx;
    qms->write_ctl_msg(buf, sz, 0);
  }

  static void write_dec2enc (void *ctx, const void *buf, size_t sz)
  {
    QMINScheme *const qms = (QMINScheme *) ctx;
    qms->write_ctl_msg(buf, sz, 1);
  }

  struct qmin_enc      *qms_enc;
  struct qmin_dec      *qms_dec;

  /* Each call to `encode' is interpreted as a header block for a new
   * stream.
   */
  unsigned              qms_next_stream_id_to_encode;

  struct {
    struct qmin_ctl_out   out;
    size_t                off;
    unsigned char         buf[0x1000];
  }                     qms_ctl[2]; /* 0: enc-to-dec; 1: dec-to-enc */
};
}}
