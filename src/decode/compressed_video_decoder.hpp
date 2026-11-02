#ifndef RRD_CONVERT_SRC_DECODE_COMPRESSED_VIDEO_DECODER_HPP_
#define RRD_CONVERT_SRC_DECODE_COMPRESSED_VIDEO_DECODER_HPP_

#include "core/decoder.hpp"

#include <string>
#include <unordered_set>

namespace rrd_convert {

class CompressedVideoDecoder : public Decoder {
 public:
  std::string type_name() const override;
  DecodeResult decode(const InputRecord& record,
                      DecodeContext& context) override;

 private:
  std::unordered_set<std::string> keyframe_topics_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_DECODE_COMPRESSED_VIDEO_DECODER_HPP_
