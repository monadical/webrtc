/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "webrtc/base/array_view.h"
#include "webrtc/modules/audio_coding/codecs/pcm16b/audio_encoder_pcm16b.h"
#include "webrtc/modules/audio_coding/neteq/tools/audio_checksum.h"
#include "webrtc/modules/audio_coding/neteq/tools/encode_neteq_input.h"
#include "webrtc/modules/audio_coding/neteq/tools/neteq_test.h"
#include "webrtc/modules/rtp_rtcp/source/byte_io.h"
#include "webrtc/test/testsupport/fileutils.h"

namespace webrtc {
namespace test {
namespace {
constexpr int kPayloadType = 95;

class FuzzRtpInput : public NetEqInput {
 public:
  explicit FuzzRtpInput(rtc::ArrayView<const uint8_t> data) : data_(data) {
    std::unique_ptr<InputAudioFile> audio_input(
        new InputAudioFile(ResourcePath("audio_coding/testfile32kHz", "pcm")));
    AudioEncoderPcm16B::Config config;
    config.payload_type = kPayloadType;
    config.sample_rate_hz = 32000;
    std::unique_ptr<AudioEncoder> encoder(new AudioEncoderPcm16B(config));
    input_.reset(new EncodeNetEqInput(std::move(audio_input),
                                      std::move(encoder),
                                      std::numeric_limits<int64_t>::max()));
    packet_ = input_->PopPacket();
    FuzzHeader();
  }

  rtc::Optional<int64_t> NextPacketTime() const override {
    return rtc::Optional<int64_t>(packet_->time_ms);
  }

  rtc::Optional<int64_t> NextOutputEventTime() const override {
    return input_->NextOutputEventTime();
  }

  std::unique_ptr<PacketData> PopPacket() override {
    RTC_DCHECK(packet_);
    std::unique_ptr<PacketData> packet_to_return = std::move(packet_);
    packet_ = input_->PopPacket();
    FuzzHeader();
    return packet_to_return;
  }

  void AdvanceOutputEvent() override { return input_->AdvanceOutputEvent(); }

  bool ended() const override { return ended_; }

  rtc::Optional<RTPHeader> NextHeader() const override {
    RTC_DCHECK(packet_);
    return rtc::Optional<RTPHeader>(packet_->header.header);
  }

 private:
  void FuzzHeader() {
    constexpr size_t kNumBytesToFuzz = 11;
    if (data_ix_ + kNumBytesToFuzz > data_.size()) {
      ended_ = true;
      return;
    }
    RTC_DCHECK(packet_);
    const size_t start_ix = data_ix_;
    packet_->header.header.payloadType =
        ByteReader<uint8_t>::ReadLittleEndian(&data_[data_ix_]);
    packet_->header.header.payloadType &= 0x7F;
    data_ix_ += sizeof(uint8_t);
    packet_->header.header.sequenceNumber =
        ByteReader<uint16_t>::ReadLittleEndian(&data_[data_ix_]);
    data_ix_ += sizeof(uint16_t);
    packet_->header.header.timestamp =
        ByteReader<uint32_t>::ReadLittleEndian(&data_[data_ix_]);
    data_ix_ += sizeof(uint32_t);
    packet_->header.header.ssrc =
        ByteReader<uint32_t>::ReadLittleEndian(&data_[data_ix_]);
    data_ix_ += sizeof(uint32_t);
    RTC_CHECK_EQ(data_ix_ - start_ix, kNumBytesToFuzz);
  }

  bool ended_ = false;
  rtc::ArrayView<const uint8_t> data_;
  size_t data_ix_ = 0;
  std::unique_ptr<EncodeNetEqInput> input_;
  std::unique_ptr<PacketData> packet_;
};
}  // namespace

void FuzzOneInputTest(const uint8_t* data, size_t size) {
  std::unique_ptr<FuzzRtpInput> input(
      new FuzzRtpInput(rtc::ArrayView<const uint8_t>(data, size)));
  std::unique_ptr<AudioChecksum> output(new AudioChecksum);
  NetEqTestErrorCallback dummy_callback;  // Does nothing with error callbacks.
  NetEq::Config config;
  NetEqTest::DecoderMap codecs;
  codecs[0] = std::make_pair(NetEqDecoder::kDecoderPCMu, "pcmu");
  codecs[8] = std::make_pair(NetEqDecoder::kDecoderPCMa, "pcma");
  codecs[102] = std::make_pair(NetEqDecoder::kDecoderILBC, "ilbc");
  codecs[103] = std::make_pair(NetEqDecoder::kDecoderISAC, "isac");
  codecs[104] = std::make_pair(NetEqDecoder::kDecoderISACswb, "isac-swb");
  codecs[111] = std::make_pair(NetEqDecoder::kDecoderOpus, "opus");
  codecs[93] = std::make_pair(NetEqDecoder::kDecoderPCM16B, "pcm16-nb");
  codecs[94] = std::make_pair(NetEqDecoder::kDecoderPCM16Bwb, "pcm16-wb");
  codecs[96] =
      std::make_pair(NetEqDecoder::kDecoderPCM16Bswb48kHz, "pcm16-swb48");
  codecs[9] = std::make_pair(NetEqDecoder::kDecoderG722, "g722");
  codecs[106] = std::make_pair(NetEqDecoder::kDecoderAVT, "avt");
  codecs[117] = std::make_pair(NetEqDecoder::kDecoderRED, "red");
  codecs[13] = std::make_pair(NetEqDecoder::kDecoderCNGnb, "cng-nb");
  codecs[98] = std::make_pair(NetEqDecoder::kDecoderCNGwb, "cng-wb");
  codecs[99] = std::make_pair(NetEqDecoder::kDecoderCNGswb32kHz, "cng-swb32");
  codecs[100] = std::make_pair(NetEqDecoder::kDecoderCNGswb48kHz, "cng-swb48");
  // This is the payload type that will be used for encoding.
  codecs[kPayloadType] =
      std::make_pair(NetEqDecoder::kDecoderPCM16Bswb32kHz, "pcm16-swb32");
  NetEqTest::ExtDecoderMap ext_codecs;

  NetEqTest test(config, codecs, ext_codecs, std::move(input),
                 std::move(output), &dummy_callback);
  test.Run();
}

}  // namespace test

void FuzzOneInput(const uint8_t* data, size_t size) {
  test::FuzzOneInputTest(data, size);
}

}  // namespace webrtc