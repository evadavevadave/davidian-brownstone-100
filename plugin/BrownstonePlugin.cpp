/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* VST3 / Audio Unit wrapper (DPF) around the same DSP the LV2 plugin uses. The DSP is
 * driven through its LV2-style entry points, so all formats share one code path.
 * DPF parameter i is LV2 control port i + 2 (ports 0 and 1 are the audio ports). */
#include "DistrhoPlugin.hpp"

#include <cstring>

extern "C" {
#include "lv2_core.h"
#include "ports.h"
const LV2_Descriptor* brownstone_dsp_descriptor(void);
}

START_NAMESPACE_DISTRHO

static const uint32_t kParamCount = P_COUNT - 2;

class BrownstonePlugin : public Plugin
{
public:
    BrownstonePlugin()
        : Plugin(kParamCount, 0, 0),
          fDesc(brownstone_dsp_descriptor()),
          fDsp(nullptr)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            Parameter p;
            initParameter(i, p);
            fValues[i] = p.ranges.def;
        }
        createDsp(getSampleRate());
    }

    ~BrownstonePlugin() override
    {
        destroyDsp();
    }

protected:
    const char* getLabel() const override { return "Brownstone100"; }
    const char* getDescription() const override
    {
        return "Two-channel high-gain valve amp simulation with tone stack, power amp and "
               "speaker cabinet models.";
    }
    const char* getMaker() const override { return "Davidian"; }
    const char* getHomePage() const override
    {
        return "https://github.com/evadavevadave/davidian-brownstone-100";
    }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(1, 1, 0); }


    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        p.ranges.min = 0.0f;
        p.ranges.max = 10.0f;
        p.ranges.def = 5.0f;

        switch (index + 2)
        {
        case P_CHANNEL:
        {
            p.name = "Channel"; p.symbol = "channel";
            p.hints |= kParameterIsInteger;
            p.ranges.max = 2.0f; p.ranges.def = 2.0f;
            ParameterEnumerationValue* v = new ParameterEnumerationValue[3];
            v[0].value = 0.0f; v[0].label = "Rhythm";
            v[1].value = 1.0f; v[1].label = "Rhythm Crunch";
            v[2].value = 2.0f; v[2].label = "Lead";
            p.enumValues.count = 3;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = v;
            p.enumValues.deleteLater = true;
            break;
        }
        case P_INPUT_LOW:
            p.name = "Low Input"; p.symbol = "input_low";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges.max = 1.0f; p.ranges.def = 0.0f;
            break;
        case P_RHY_PRE:   p.name = "Rhythm Pre";  p.symbol = "rhythm_pre";  p.ranges.def = 4.0f; break;
        case P_RHY_POST:  p.name = "Rhythm Post"; p.symbol = "rhythm_post"; p.ranges.def = 6.0f; break;
        case P_LEAD_PRE:  p.name = "Lead Pre";    p.symbol = "lead_pre";    p.ranges.def = 6.0f; break;
        case P_LEAD_POST: p.name = "Lead Post";   p.symbol = "lead_post";   p.ranges.def = 3.0f; break;
        case P_LOW:       p.name = "Low";         p.symbol = "low";         break;
        case P_MID:       p.name = "Mid";         p.symbol = "mid";         break;
        case P_HIGH:      p.name = "High";        p.symbol = "high";        break;
        case P_PRESENCE:  p.name = "Presence";    p.symbol = "presence";    break;
        case P_RESONANCE: p.name = "Resonance";   p.symbol = "resonance";   break;
        case P_CAB:
            p.name = "Cabinet Sim"; p.symbol = "cab";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges.max = 1.0f; p.ranges.def = 1.0f;
            break;
        case P_IN_TRIM:
            p.name = "Input Trim"; p.symbol = "input_trim"; p.unit = "dB";
            p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
            break;
        case P_OUT_LEVEL:
            p.name = "Output Level"; p.symbol = "output"; p.unit = "dB";
            p.ranges.min = -60.0f; p.ranges.max = 12.0f; p.ranges.def = 0.0f;
            break;
        case P_CAB_MODEL:
        {
            p.name = "Cabinet Model"; p.symbol = "cab_model";
            p.hints |= kParameterIsInteger;
            p.ranges.max = 3.0f; p.ranges.def = 0.0f;
            ParameterEnumerationValue* v = new ParameterEnumerationValue[4];
            v[0].value = 0.0f; v[0].label = "4x12 Modern";
            v[1].value = 1.0f; v[1].label = "4x12 Vintage";
            v[2].value = 2.0f; v[2].label = "2x12 Open Back";
            v[3].value = 3.0f; v[3].label = "1x12 Combo";
            p.enumValues.count = 4;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = v;
            p.enumValues.deleteLater = true;
            break;
        }
        default:
            break;
        }
        p.shortName = p.name;
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? fValues[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < kParamCount)
            fValues[index] = value; /* the DSP reads it through its connected port */
    }

    void activate() override
    {
        if (fDsp != nullptr && fDesc->activate != nullptr)
            fDesc->activate(fDsp);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        if (fDsp == nullptr)
        {
            std::memset(outputs[0], 0, sizeof(float) * frames);
            return;
        }
        fDesc->connect_port(fDsp, P_IN, const_cast<float*>(inputs[0]));
        fDesc->connect_port(fDsp, P_OUT, outputs[0]);
        fDesc->run(fDsp, frames);
    }

    void sampleRateChanged(double newSampleRate) override
    {
        destroyDsp();
        createDsp(newSampleRate);
        activate();
    }

private:
    void createDsp(double rate)
    {
        if (rate <= 0.0)
            rate = 48000.0;
        fDsp = fDesc->instantiate(fDesc, rate, "", nullptr);
        if (fDsp == nullptr)
            return;
        for (uint32_t i = 0; i < kParamCount; ++i)
            fDesc->connect_port(fDsp, i + 2, &fValues[i]);
    }

    void destroyDsp()
    {
        if (fDsp != nullptr)
        {
            fDesc->cleanup(fDsp);
            fDsp = nullptr;
        }
    }

    const LV2_Descriptor* const fDesc;
    LV2_Handle fDsp;
    float fValues[kParamCount];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrownstonePlugin)
};

Plugin* createPlugin()
{
    return new BrownstonePlugin();
}

END_NAMESPACE_DISTRHO
