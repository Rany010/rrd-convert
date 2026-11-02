#ifndef RRD_CONVERT_SRC_DECODE_TF_DECODER_HPP_
#define RRD_CONVERT_SRC_DECODE_TF_DECODER_HPP_

#include "core/decoder.hpp"

namespace rrd_convert {

class TfMessageDecoder : public Decoder {
 public:
  std::string type_name() const override;
  DecodeResult decode(const InputRecord& record,
                      DecodeContext& context) override;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_DECODE_TF_DECODER_HPP_
