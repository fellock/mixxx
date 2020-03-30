#pragma once

#include "audio/types.h"
#include "util/assert.h"
#include "util/macros.h"
#include "util/optional.h"

namespace mixxx {

namespace audio {

// Properties that characterize an uncompressed PCM audio signal.
class SignalInfo final {
    // Properties
    PROPERTY_SET_BYVAL_GET_BYREF(ChannelCount, channelCount, ChannelCount)
    PROPERTY_SET_BYVAL_GET_BYREF(SampleRate, sampleRate, SampleRate)
    PROPERTY_SET_BYVAL_GET_BYREF(OptionalSampleLayout, sampleLayout, SampleLayout)

  public:
    constexpr SignalInfo() = default;
    constexpr explicit SignalInfo(
            OptionalSampleLayout sampleLayout)
            : m_sampleLayout(sampleLayout) {
    }
    SignalInfo(
            ChannelCount channelCount,
            SampleRate sampleRate,
            OptionalSampleLayout sampleLayout = std::nullopt)
            : m_channelCount(channelCount),
              m_sampleRate(sampleRate),
              m_sampleLayout(sampleLayout) {
    }
    SignalInfo(SignalInfo&&) = default;
    SignalInfo(const SignalInfo&) = default;
    /*non-virtual*/ ~SignalInfo() = default;

    constexpr bool isValid() const {
        return getChannelCount().isValid() &&
                getSampleLayout() &&
                getSampleRate().isValid();
    }

    SignalInfo& operator=(SignalInfo&&) = default;
    SignalInfo& operator=(const SignalInfo&) = default;

    // Conversion: #samples / sample offset -> #frames / frame offset
    // Only works for sample offsets on frame boundaries!
    template<typename T>
    T samples2frames(T samples) const {
        DEBUG_ASSERT(getChannelCount().isValid());
        DEBUG_ASSERT(0 == (samples % getChannelCount()));
        return samples / getChannelCount();
    }

    // Conversion: #frames / frame offset -> #samples / sample offset
    template<typename T>
    T frames2samples(T frames) const {
        DEBUG_ASSERT(getChannelCount().isValid());
        return frames * getChannelCount();
    }

    // Conversion: #frames / frame offset -> second offset
    template<typename T>
    double frames2secs(T frames) const {
        DEBUG_ASSERT(getSampleRate().isValid());
        return static_cast<double>(frames) / getSampleRate();
    }

    // Conversion: second offset -> #frames / frame offset
    double secs2frames(double seconds) const {
        DEBUG_ASSERT(getSampleRate().isValid());
        return seconds * getSampleRate();
    }

    // Conversion: #frames / frame offset -> millisecond offset
    template<typename T>
    double frames2millis(T frames) const {
        return frames2secs(frames) * 1000;
    }

    // Conversion: millisecond offset -> #frames / frame offset
    double millis2frames(double milliseconds) const {
        return secs2frames(milliseconds / 1000);
    }

    // Conversion: #samples / sample offset -> second offset
    // Only works for sample offsets on frame boundaries!
    template<typename T>
    double samples2secs(T samples) const {
        return frames2secs(samples2frames(samples));
    }

    // Conversion: second offset -> #samples / sample offset
    // May return sample offsets that are not on frame boundaries!
    template<typename T>
    double secs2samples(double seconds) const {
        return frames2samples(secs2frames(seconds));
    }

    // Conversion: #samples / sample offset -> millisecond offset
    // Only works for sample offsets on frame boundaries!
    template<typename T>
    double samples2millis(T samples) const {
        return frames2millis(samples2frames(samples));
    }

    // Conversion: millisecond offset -> #samples / sample offset
    // May return sample offsets that are not on frame boundaries!
    double millis2samples(double milliseconds) const {
        return frames2samples(millis2frames(milliseconds));
    }
};

bool operator==(
        const SignalInfo& lhs,
        const SignalInfo& rhs);

inline bool operator!=(
        const SignalInfo& lhs,
        const SignalInfo& rhs) {
    return !(lhs == rhs);
}

QDebug operator<<(QDebug dbg, const SignalInfo& arg);

} // namespace audio

} // namespace mixxx

Q_DECLARE_METATYPE(mixxx::audio::SignalInfo)
