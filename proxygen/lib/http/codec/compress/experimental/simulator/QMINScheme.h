#pragma once

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
    qms_ctl[0].qco_write = write_enc2dec;
    qms_ctl[0].qco_ctx = this;
    qms_ctl[1].qco_write = write_dec2enc;
    qms_ctl[0].qco_ctx = this;

    qms_enc = qmin_enc_new(QSIDE_CLIENT, 64 * 1024, &qms_ctl[0]);
    qms_dec = qmin_dec_new(QSIDE_SERVER, 64 * 1024, &qms_ctl[1]);

    qms_next_stream_id_to_encode = 1;
  }

  ~QMINScheme()
  {
    qmin_enc_destroy(qms_enc);
    qmin_dec_destroy(qms_dec);
  }

  std::unique_ptr<CompressionScheme::Ack> getAck(uint16_t seqn) override
  {
    return nullptr;
  }

  void recvAck(std::unique_ptr<CompressionScheme::Ack> ack) override
  {
    return;
  }

  std::pair<bool, std::unique_ptr<folly::IOBuf>> encode(
    std::vector<compress::Header> allHeaders, SimStats& stats) override
  {
    unsigned char outbuf[0x1000];
    size_t nw, off;
    enum qmin_encode_status qes;

    off = 0;

    for (const auto header : allHeaders) {
      std::string name{header.name->c_str()};
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      qes = qmin_enc_encode(qms_enc, qms_next_stream_id_to_encode, name.c_str(),
        name.length(), header.value->c_str(), header.value->length(), QIT_YES,
        outbuf + off, sizeof(outbuf) - off, &nw);
      switch (qes)
      {
      case QES_OK:
        off += nw;
        break;
      case QES_NOBUFS:
        VLOG(1) << "compressed header does not fit into temporary output buffer";
        return {false, nullptr};
      case QES_ERR:
        VLOG(1) << "error: " << strerror(errno);
        return {false, nullptr};
      }
    }

    qms_next_stream_id_to_encode += 2;
    return {false, folly::IOBuf::copyBuffer(outbuf, off)};
  }

  void decode(bool allowOOO, std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats& stats, SimStreamingCallback& callback) override
  {
    return;
  }

  uint32_t getHolBlockCount() const override
  {
    return 0;
  }

  void runLoopCallback() noexcept override
  {
    return;
  }

  static void write_enc2dec (void *qco_ctx, const void *buf, size_t sz)
  {
    VLOG(1) << "Throw away encoder-to-decoder control message";
  }

  static void write_dec2enc (void *qco_ctx, const void *buf, size_t sz)
  {
    VLOG(1) << "Throw away decoder-to-encoder control message";
  }

  struct qmin_ctl_out   qms_ctl[2]; /* 0: encoder-to-decoder;
                                     * 1: decoder-to-encoder.
                                     */

  struct qmin_enc      *qms_enc;
  struct qmin_dec      *qms_dec;

  /* Each call to `encode' is interpreted as a header block for a new
   * stream.
   */
  unsigned              qms_next_stream_id_to_encode;
};
}}
