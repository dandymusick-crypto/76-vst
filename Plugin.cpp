#include <JuceHeader.h>
#include <BinaryData.h>

//==============================================================================
// LookAndFeel minimalista para dibujar indicadores sobre el hardware 3D
class Dandy76LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider&) override
    {
        auto radius = (float)juce::jmin(width / 2, height / 2) - 6.0f;
        auto centreX = (float)x + (float)width * 0.5f;
        auto centreY = (float)y + (float)height * 0.5f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Arco sutil exterior y línea indicadora
        juce::Path arc;
        arc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colours::orange.withAlpha(0.75f));
        g.strokePath(arc, juce::PathStrokeType(2.5f));

        juce::Path pointer;
        pointer.addRectangle(-1.5f, -radius, 3.0f, radius * 0.35f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                          bool shouldDrawButtonAsHighlighted, bool) override
    {
        if (b.getToggleState())
        {
            g.setColour(juce::Colours::orange.withAlpha(0.45f));
            g.fillRoundedRectangle(b.getLocalBounds().toFloat(), 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRoundedRectangle(b.getLocalBounds().toFloat(), 4.0f, 1.5f);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.fillRoundedRectangle(b.getLocalBounds().toFloat(), 4.0f);
        }
    }
};

//==============================================================================
class Dandy76AudioProcessor : public juce::AudioProcessor
{
public:
    Dandy76AudioProcessor()
        : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
    }

    ~Dandy76AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, (juce::uint32)getTotalNumOutputChannels() };
        scFilter.prepare(spec);
        scFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        envelope = 0.0f;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        auto mainOut = layouts.getMainOutputChannelSet();
        if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
            return false;
        return mainOut == layouts.getMainInputChannelSet();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        if (apvts.getRawParameterValue("BYPASS")->load() > 0.5f)
        {
            currentGrDb.store(0.0f);
            return;
        }

        const float inParam  = apvts.getRawParameterValue("INPUT")->load();     // 0..50
        const float outParam = apvts.getRawParameterValue("OUTPUT")->load();    // 0..50
        const float attParam = apvts.getRawParameterValue("ATTACK")->load();    // 1..7 (1=Fast, 7=Slow)
        const float relParam = apvts.getRawParameterValue("RELEASE")->load();   // 1..7 (1=Fast, 7=Slow)
        const int ratioIdx   = (int)apvts.getRawParameterValue("RATIO")->load();// 0..4
        const float mixParam = apvts.getRawParameterValue("MIX")->load() * 0.01f;
        const float scFreq   = apvts.getRawParameterValue("SC_FILTER")->load(); // 20..250 Hz
        const bool gritOn    = apvts.getRawParameterValue("GRIT")->load() > 0.5f;

        const float inGain   = juce::Decibels::decibelsToGain((inParam - 20.0f) * 0.9f);
        const float outGain  = juce::Decibels::decibelsToGain((outParam - 20.0f) * 0.9f);

        // Tiempos estilo FET (1 = Rápido, 7 = Lento según serigrafía del panel)
        const float attackMs  = juce::jmap(attParam, 1.0f, 7.0f, 0.05f, 5.0f);
        const float releaseMs = juce::jmap(relParam, 1.0f, 7.0f, 50.0f, 1100.0f);
        const float attCoeff  = std::exp(-1.0f / (0.001f * attackMs * (float)currentSampleRate));
        const float relCoeff  = std::exp(-1.0f / (0.001f * releaseMs * (float)currentSampleRate));

        const float ratios[5] = { 4.0f, 8.0f, 12.0f, 20.0f, 25.0f };
        const float ratio     = ratios[juce::jlimit(0, 4, ratioIdx)];
        const bool allButtons = (ratioIdx == 4);
        const float threshDb  = allButtons ? -24.0f : -18.0f;

        scFilter.setCutoffFrequency(scFreq);

        const int numCh = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        float maxGrDb = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            // Detector Sidechain con HPF
            float det = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                det = std::max(det, std::abs(buffer.getSample(ch, s) * inGain));

            float filteredDet = std::abs(scFilter.processSample(0, det));

            if (filteredDet > envelope)
                envelope = attCoeff * envelope + (1.0f - attCoeff) * filteredDet;
            else
                envelope = relCoeff * envelope + (1.0f - relCoeff) * filteredDet;

            float envDb = juce::Decibels::gainToDecibels(envelope + 1e-6f);
            float grDb = 0.0f;
            if (envDb > threshDb)
                grDb = (envDb - threshDb) * (1.0f - (1.0f / ratio));

            maxGrDb = std::max(maxGrDb, grDb);
            float compGain = juce::Decibels::decibelsToGain(-grDb);

            for (int ch = 0; ch < numCh; ++ch)
            {
                float dry = buffer.getSample(ch, s);
                float wet = dry * inGain * compGain;

                if (gritOn || allButtons)
                {
                    float drive = (gritOn && allButtons) ? 2.4f : 1.65f;
                    wet = std::tanh(wet * drive) / std::tanh(drive);
                }

                wet *= outGain;
                buffer.setSample(ch, s, dry * (1.0f - mixParam) + wet * mixParam);
            }
        }

        currentGrDb.store(maxGrDb);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dandy 76"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> currentGrDb { 0.0f };

private:
    double currentSampleRate = 44100.0;
    float envelope = 0.0f;
    juce::dsp::StateVariableTPTFilter<float> scFilter;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
        p.push_back(std::make_unique<juce::AudioParameterFloat>("INPUT", "Input", 0.0f, 50.0f, 25.0f));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("OUTPUT", "Output", 0.0f, 50.0f, 25.0f));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", 1.0f, 7.0f, 4.0f));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", 1.0f, 7.0f, 4.0f));
        p.push_back(std::make_unique<juce::AudioParameterChoice>("RATIO", "Ratio", juce::StringArray{ "4", "8", "12", "20", "ALL" }, 0));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("MIX", "Mix", 0.0f, 100.0f, 100.0f));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("SC_FILTER", "SC Filter", 20.0f, 250.0f, 20.0f));
        p.push_back(std::make_unique<juce::AudioParameterBool>("GRIT", "Grit", false));
        p.push_back(std::make_unique<juce::AudioParameterBool>("BYPASS", "Bypass", false));
        return { p.begin(), p.end() };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dandy76AudioProcessor)
};

//==============================================================================
class Dandy76AudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    Dandy76AudioProcessorEditor(Dandy76AudioProcessor& p)
        : AudioProcessorEditor(&p), proc(p)
    {
        setLookAndFeel(&lnf);
        bg = juce::ImageCache::getFromMemory(BinaryData::Gemini_Generated_Image_y3klsty3klsty3kl_jfif,
                                             BinaryData::Gemini_Generated_Image_y3klsty3klsty3kl_jfifSize);

        setupKnob(inSlider, "INPUT", inAtt);
        setupKnob(outSlider, "OUTPUT", outAtt);
        setupKnob(attSlider, "ATTACK", attAtt);
        setupKnob(relSlider, "RELEASE", relAtt);
        setupKnob(mixSlider, "MIX", mixAtt);
        setupKnob(scSlider, "SC_FILTER", scAtt);

        addAndMakeVisible(gritBtn);
        gritAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "GRIT", gritBtn);

        addAndMakeVisible(bypassBtn);
        bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "BYPASS", bypassBtn);

        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible(ratioBtns[i]);
            ratioBtns[i].setRadioGroupId(1001);
            ratioBtns[i].onClick = [this, i] {
                if (ratioBtns[i].getToggleState())
                    proc.apvts.getParameter("RATIO")->setValueNotifyingHost((float)i / 4.0f);
            };
        }
        ratioBtns[0].setToggleState(true, juce::dontSendNotification);

        setSize(1100, 468);
        startTimerHz(30);
    }

    ~Dandy76AudioProcessorEditor() override
    {
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.drawImage(bg, getLocalBounds().toFloat());

        // Aguja animada sobre el vúmetro (VU Meter)
        float gr = juce::jlimit(0.0f, 20.0f, proc.currentGrDb.load());
        float norm = 1.0f - (gr / 20.0f); // Reposo a la derecha (0 dB), se mueve a la izquierda con GR
        float angle = juce::jmap(norm, 0.0f, 1.0f, -0.65f, 0.45f);

        float pivotX = getWidth() * 0.728f;
        float pivotY = getHeight() * 0.435f;
        float needleLen = getHeight() * 0.21f;

        juce::Path needle;
        needle.addLineSegment(juce::Line<float>(0.0f, 0.0f, 0.0f, -needleLen), 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillPath(needle, juce::AffineTransform::rotation(angle).translated(pivotX, pivotY));
    }

    void resized() override
    {
        // Mapeado exacto sobre las perillas y botones de la imagen
        inSlider.setBoundsRelative(0.088f, 0.285f, 0.120f, 0.310f);
        outSlider.setBoundsRelative(0.262f, 0.285f, 0.120f, 0.310f);
        attSlider.setBoundsRelative(0.435f, 0.205f, 0.068f, 0.165f);
        relSlider.setBoundsRelative(0.435f, 0.465f, 0.068f, 0.165f);

        for (int i = 0; i < 5; ++i)
            ratioBtns[i].setBoundsRelative(0.578f, 0.170f + i * 0.066f, 0.024f, 0.058f);

        mixSlider.setBoundsRelative(0.863f, 0.160f, 0.058f, 0.140f);
        scSlider.setBoundsRelative(0.863f, 0.348f, 0.058f, 0.140f);
        gritBtn.setBoundsRelative(0.875f, 0.535f, 0.035f, 0.055f);
        bypassBtn.setBoundsRelative(0.868f, 0.605f, 0.048f, 0.115f);
    }

private:
    void timerCallback() override
    {
        int rIdx = (int)proc.apvts.getRawParameterValue("RATIO")->load();
        if (rIdx >= 0 && rIdx < 5)
            ratioBtns[rIdx].setToggleState(true, juce::dontSendNotification);
        repaint();
    }

    void setupKnob(juce::Slider& s, const juce::String& id,
                   std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled(true, true, this);
        addAndMakeVisible(s);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, s);
    }

    Dandy76AudioProcessor& proc;
    Dandy76LookAndFeel lnf;
    juce::Image bg;

    juce::Slider inSlider, outSlider, attSlider, relSlider, mixSlider, scSlider;
    juce::ToggleButton gritBtn, bypassBtn, ratioBtns[5];

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAtt, outAtt, attAtt, relAtt, mixAtt, scAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gritAtt, bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dandy76AudioProcessorEditor)
};

juce::AudioProcessorEditor* Dandy76AudioProcessor::createEditor()
{
    return new Dandy76AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Dandy76AudioProcessor();
}
