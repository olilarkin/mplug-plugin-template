#pragma once

#include <mplug/mplug.h>
#include <mplug/mplug_dsp.h>
#include <mplug/mplug_editor_host.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

class MyPlugin
{
public:
  // --- Metadata --------------------------------------------------------------
  static constexpr std::string_view name() { return "My Plugin"; }

  // --- Audio bus configuration: stereo in, stereo out ------------------------
  static constexpr std::size_t inputBusCount() { return 1; }
  static constexpr std::size_t outputBusCount() { return 1; }

  static constexpr mplug::BusInfo inputBusInfo(std::size_t index)
  {
    if (index == 0) return {"Input", 2, mplug::BusType::Main};
    return {};
  }

  static constexpr mplug::BusInfo outputBusInfo(std::size_t index)
  {
    if (index == 0) return {"Output", 2, mplug::BusType::Main};
    return {};
  }

  // --- Parameters: a single gain parameter -----------------------------------
  static constexpr std::size_t parameterCount() { return 1; }

  static constexpr mplug::ParameterInfo parameterInfo(std::size_t index)
  {
    if (index == 0)
    {
      return {
        .name = "Gain",
        .shortName = "Gain",
        .minValue = -60.0,
        .maxValue = 12.0,
        .defaultValue = 0.0,
        .flags = mplug::ParameterFlags::Automatable,
        .unit = mplug::ParameterUnit::Decibels
      };
    }
    return {};
  }

  // --- Latency reporting -----------------------------------------------------
  // Report the plugin's processing latency in samples so the host can
  // compensate. A plain gain has no latency, so this returns 0 — it's here as
  // plumbing: if you add lookahead or an FFT, return the real sample count.
  // (A zero-latency effect could equally omit this method entirely.)
  static constexpr std::uint32_t latency() { return 0; }

  // --- Editor ----------------------------------------------------------------
  static constexpr bool hasEditor() { return true; }
  static constexpr mplug::EditorSize defaultEditorSize() { return {400, 300}; }

  void* createEditor(void* parentView, mplug::WindowType windowType);
  void destroyEditor();

  // Optional hook: the format wrapper hands us an upward channel to the host
  // just before createEditor(). The editor uses it to record parameter gestures
  // and to receive host-originated parameter changes. Wrappers that don't
  // support it never call this, leaving mEditorHost null (editor falls back to
  // direct setParameterValue — audio updates, no automation).
  void setEditorHost(mplug::EditorHost* host) { mEditorHost = host; }

  // --- Lifecycle -------------------------------------------------------------
  void prepare(double sampleRate, int maxBlockSize)
  {
    mSampleRate = sampleRate;
    mMaxBlockSize = maxBlockSize;
  }

  void reset()
  {
    // Reset any internal DSP state here.
  }

  // --- Audio processing ------------------------------------------------------
  void process(mplug::AudioInputsView inputs, mplug::AudioOutputsView outputs) noexcept
  {
    const auto numSamples = outputs.getNumFrames();
    const auto numChannels = std::min(inputs.getNumChannels(), outputs.getNumChannels());

    // dB -> linear (atomic load for thread-safe access from the UI)
    const float linearGain = static_cast<float>(mplug::dsp::dbToLinear(mGainDb.load(std::memory_order_relaxed)));

    for (std::size_t ch = 0; ch < numChannels; ++ch)
    {
      auto* in = inputs.getChannel(ch).data.data;
      auto* out = outputs.getChannel(ch).data.data;

      for (std::size_t i = 0; i < numSamples; ++i)
        out[i] = in[i] * linearGain;
    }
  }

  // --- Parameter access (atomic for thread-safe UI reads) --------------------
  double getParameterValue(std::size_t index) const
  {
    if (index == 0) return mGainDb.load(std::memory_order_relaxed);
    return 0.0;
  }

  void setParameterValue(std::size_t index, double value)
  {
    if (index == 0) mGainDb.store(value, std::memory_order_relaxed);
  }

  // --- State -----------------------------------------------------------------
  // Parameter values are auto-serialized by mplug::serializeState. Implement
  // saveCustomState/loadCustomState to persist additional, non-parameter state
  // alongside them (here: a free-text note shown in the editor). These are
  // called on the main thread, so a plain std::string is fine.
  choc::value::Value saveCustomState() const
  {
    auto state = choc::value::createObject("MyPluginState");
    state.addMember("editorNote", mEditorNote);
    return state;
  }

  void loadCustomState(const choc::value::ValueView& state)
  {
    if (state.isObject() && state.hasObjectMember("editorNote"))
      mEditorNote = state["editorNote"].getWithDefault<std::string>("");
  }

private:
  double mSampleRate = 44100.0;
  int mMaxBlockSize = 512;

  std::atomic<double> mGainDb{0.0};  // Default: 0 dB (unity gain)

  // Example of extra, non-parameter state persisted via saveCustomState.
  std::string mEditorNote;

  // Opaque, platform-specific editor handle (managed in the editor sources).
  void* mEditorView = nullptr;

  // Upward channel to the host, supplied by the wrapper via setEditorHost().
  // Null when the active format/wrapper doesn't provide one.
  mplug::EditorHost* mEditorHost = nullptr;
};

// Verify the plugin satisfies the MPlug Plugin concept at compile time.
static_assert(mplug::Plugin<MyPlugin>);
