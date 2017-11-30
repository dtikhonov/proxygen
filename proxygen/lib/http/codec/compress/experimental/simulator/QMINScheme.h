#pragma once

#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionScheme.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKQueue.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>

namespace proxygen {  namespace compress {
class QMINScheme : public CompressionScheme {
 public:
  explicit QMINScheme(CompressionSimulator* sim)
      : CompressionScheme(sim)
  {
  }

  ~QMINScheme()
  {
  }

  std::unique_ptr<CompressionScheme::Ack> getAck(uint16_t seqn) override
  {
    return nullptr;
  }

  void recvAck(std::unique_ptr<CompressionScheme::Ack> ack) override
  {
    return nullptr;
  }

  std::pair<bool, std::unique_ptr<folly::IOBuf>> encode(
    std::vector<compress::Header> allHeaders, SimStats& stats) override
  {
    return {false, nullptr};
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
};
}}
