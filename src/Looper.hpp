struct Looper : Module {
  enum ParamIds {
    MODE_TOGGLE_PARAM,
    ERASE_BUTTON_PARAM,
    UNDO_BUTTON_PARAM,
    STOP_BUTTON_PARAM,
    FEEDBACK_PARAM,
    RETURN_BUTTON_PARAM,
    RETURN_ENABLED_PARAM,
    MIX_PARAM,
    NUM_PARAMS
  };

  enum InputIds {
    ARM_CV_INPUT,
    MODE_CV_INPUT,
    ERASE_CV_INPUT,
    UNDO_CV_INPUT,
    STOP_CV_INPUT,
    RETURN_1_INPUT,
    RETURN_2_INPUT,
    FEEDBACK_CV_INPUT,
    MIX_CV_INPUT,
    MAIN_1_INPUT,
    MAIN_2_INPUT,
    RETURN_MOD_INPUT,
    NUM_INPUTS
  };

  enum OutputIds {
    SEND_1_OUTPUT,
    SEND_2_OUTPUT,
    MAIN_1_OUTPUT,
    MAIN_2_OUTPUT,
    NUM_OUTPUTS
  };

  enum LightIds {
    ARM_STATUS_LIGHT,
    RECORD_STATUS_LIGHT,
    PLAY_STATUS_LIGHT,
    RETURN_LIGHT,
    NUM_LIGHTS
  };

  dsp::BooleanTrigger armTrigger;
  dsp::BooleanTrigger toggleTrigger;
  dsp::BooleanTrigger stopTrigger;
  dsp::BooleanTrigger eraseTrigger;
  dsp::BooleanTrigger eraseButtonTrigger;
  dsp::BooleanTrigger rtrnButtonTrigger;

  dsp::SlewLimiter smoothInGate;
  dsp::SlewLimiter smoothOutGate;

  dsp::ClockDivider lightDivider;
  dsp::ClockDivider uiDivider;

  dsp::PulseGenerator restartPulse;
  dsp::PulseGenerator togglePulse;
  float blinkTime = 0.1f;

  unsigned int ins[PORTS];
  unsigned int rtrns[PORTS];
  unsigned int snds[PORTS];
  unsigned int outs[PORTS];

  FileSaver fileSaver;
  SwitchingOrder switchingOrder = RECORD_PLAY_OVERDUB;
  Mode mode = STOPPED;
  MultiLoop loop;

  bool armed = false;
  float feedback = 1.0f;
  float mix = 1.0f;

  std::string fileFormat = "wav";
  std::string filePolyMode = "sum";
  int fileBitDepth = 16;

  float t = 0.0f;

  Looper() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    configParam(MODE_TOGGLE_PARAM, 0.0f, 1.0f, 0.0f, "Toggle");
    configParam(ERASE_BUTTON_PARAM, 0.0f, 1.0f, 0.0f, "Erase");
    configParam(STOP_BUTTON_PARAM, 0.0f, 1.0f, 0.0f, "Stop");
    configParam(FEEDBACK_PARAM, 0.0f, 1.0f, 1.0f, "Feedback", "%", 0.0f, 100.0f);
    configParam(RETURN_BUTTON_PARAM, 0.0f, 1.0f, 0.0f, "Return enabled");
    configParam(RETURN_ENABLED_PARAM, 0.0f, 1.0f, 1.0f);
    configParam(MIX_PARAM, -1.0f, 1.0f, 0.0f, "Mix");

    ins[0] = MAIN_1_INPUT;
    ins[1] = MAIN_2_INPUT;

    rtrns[0] = RETURN_1_INPUT;
    rtrns[1] = RETURN_2_INPUT;

    snds[0] = SEND_1_OUTPUT;
    snds[1] = SEND_2_OUTPUT;

    outs[0] = MAIN_1_OUTPUT;
    outs[1] = MAIN_2_OUTPUT;

    smoothInGate.setRiseFall(100.f, 50.f);
    smoothOutGate.setRiseFall(100.f, 50.f);

    lightDivider.setDivision(512);
    uiDivider.setDivision(512);

    loop.resize(PORTS);
  }

  Mode getNextMode() {
    if (mode == STOPPED && loop.size == 0)
      return RECORDING;

    if (mode == RECORDING && switchingOrder == RECORD_PLAY_OVERDUB)
      return PLAYING;

    if (mode == RECORDING && switchingOrder == RECORD_OVERDUB_PLAY)
      return OVERDUBBING;

    if (mode == PLAYING)
      return OVERDUBBING;

    if (mode == OVERDUBBING)
      return PLAYING;

    if (mode == STOPPED && loop.size > 0)
      return PLAYING;

    return mode;
  }

  void toggle() {
    if (inputs[ARM_CV_INPUT].isConnected() && !armed)
      return;

    Mode nextMode = getNextMode();

    if (mode == STOPPED && nextMode == PLAYING)
      loop.rewind();

    mode = nextMode;
    armed = false;
    togglePulse.trigger(0.1f);
  }

  void stop() {
    mode = STOPPED;
  }

  void erase() {
    mode = STOPPED;
    loop.reset();
  }

  json_t *dataToJson() override {
    json_t *root = json_object();
    // json_object_set_new(root, "switchingOrder", json_string(switchingOrderStr.c_str()));
    json_object_set_new(root, "fileFormat", json_string(fileFormat.c_str()));
    json_object_set_new(root, "fileBitDepth", json_integer(fileBitDepth));
    json_object_set_new(root, "filePolyMode", json_string(filePolyMode.c_str()));
    return root;
  }

  void dataFromJson(json_t *root) override {
    // json_t *switchingOrderJson = json_object_get(root, "switchingOrder");
    // if (switchingOrderJson)
    //   switchingOrder = SWITCHING_ORDER.at(switchingOrderJson);

    json_t *fileFormatJson = json_object_get(root, "fileFormat");
    if (fileFormatJson)
      fileFormat = json_string_value(fileFormatJson);

    json_t *fileBitDepthJson = json_object_get(root, "fileBitDepth");
    if (fileBitDepthJson)
      fileBitDepth = json_number_value(fileBitDepthJson);

    json_t *filePolyModeJson = json_object_get(root, "filePolyMode");
    if (filePolyModeJson)
      filePolyMode = json_string_value(filePolyModeJson);
  }

  void process(const ProcessArgs &args) override {

    // Process arm control

    if (armTrigger.process(inputs[ARM_CV_INPUT].getVoltage() > 0.0f)) {
      armed = true;
    }

    // Process toggle control

    if (toggleTrigger.process(params[MODE_TOGGLE_PARAM].getValue() + inputs[MODE_CV_INPUT].getVoltage() > 0.0f)) {
      toggle();
    }

    // Process stop control

    if (stopTrigger.process(params[STOP_BUTTON_PARAM].getValue() + inputs[STOP_CV_INPUT].getVoltage() > 0.0f)) {
      stop();
    }

    // Process erase control

    if (eraseTrigger.process(inputs[ERASE_CV_INPUT].getVoltage() > 0.0f)) {
      erase();
    }

    if (eraseButtonTrigger.process(params[ERASE_BUTTON_PARAM].getValue() > 0.0f)) {
      erase();
    }

    // Process return enable control

    if (rtrnButtonTrigger.process(params[RETURN_BUTTON_PARAM].getValue() > 0.0f)) {
      params[RETURN_ENABLED_PARAM].setValue(1.0f - params[RETURN_ENABLED_PARAM].getValue());
    }

    bool rtrnActive = mode != STOPPED && params[RETURN_ENABLED_PARAM].getValue() > 0.0f;

    // Process feedback param

    if (mode == STOPPED) {
      feedback = 1.0f;
    } else {
      feedback = math::clamp(params[FEEDBACK_PARAM].getValue() + inputs[FEEDBACK_CV_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
    }

    // Process mix param

    mix = math::clamp(params[MIX_PARAM].getValue() + inputs[MIX_CV_INPUT].getVoltage() / 5, -1.0f, 1.0f);

    float monitorLevel = mix > 0 ? 1 - mix : 1;
    float loopLevel = mix > 0 ? 1 : 1 + mix;

    // Process return mod input

    float mod = inputs[RETURN_MOD_INPUT].isConnected() ? inputs[RETURN_MOD_INPUT].getVoltage() : 1.0f;

    // Gates

    float inGate = smoothInGate.process(args.sampleTime, mode == RECORDING || mode == OVERDUBBING ? 1.f : 0.f);
    float outGate = smoothOutGate.process(args.sampleTime, mode == STOPPED ? 0.f : 1.f);

    // Count inputs

    for (size_t p = 0; p < PORTS; p++) {
      int tracks = loop.setChannels(p, std::max(inputs[ins[p]].getChannels(), inputs[rtrns[p]].getChannels()));
      outputs[outs[p]].setChannels(tracks);
      outputs[snds[p]].setChannels(tracks);
    }

    // Grow

    loop.next(mode == RECORDING);

    // Process each main port (left, right)

    for (size_t p = 0; p < PORTS; p++) {

      // Process each polyphony channel

      for (size_t channel = 0; channel < loop.getChannels(p); channel++) {
        float in = inputs[ins[p]].getVoltage(channel);
        float rtrn = inputs[rtrns[p]].getVoltage(channel);

        float sample = loop.read(p, channel);
        float rtrnGate = rtrnActive && inputs[rtrns[p]].getChannels() >= (channel + 1) ? mod : 0.0f;
        float newSample = rtrnGate * rtrn + (1 - rtrnGate) * sample;

        loop.write(p, channel, feedback * newSample + inGate * in);

        float send = outGate * sample;
        float out = loopLevel * send;

        outputs[outs[p]].setVoltage(out, channel);
        outputs[snds[p]].setVoltage(send, channel);
      }
    }

    // Lights

    if (loop.position == 0)
      restartPulse.trigger(0.1f);

    if (lightDivider.process()) {
      float lightTime = 512 * args.sampleTime;

      float restartBlink = (1.0f - togglePulse.process(lightTime)) * restartPulse.process(lightTime);

      lights[RECORD_STATUS_LIGHT].setBrightness(0.0f);
      lights[PLAY_STATUS_LIGHT].setBrightness(0.0f);

      if (mode == RECORDING || mode == OVERDUBBING) {
        lights[RECORD_STATUS_LIGHT].setBrightness(1.0f - restartBlink * .5f);
      }

      if (mode == PLAYING || mode == OVERDUBBING) {
        lights[PLAY_STATUS_LIGHT].setBrightness(1.0f - restartBlink);
      }

      if (mode == STOPPED && loop.size > 0) {
        float w = sin(6.f * M_PI * t) / 2 + 0.5f;
        lights[PLAY_STATUS_LIGHT].setBrightness(w / 3.f);
      }

      lights[ARM_STATUS_LIGHT].value = armed;
      lights[RETURN_LIGHT].value = inputs[rtrns[0]].isConnected() || inputs[rtrns[1]].isConnected() ? params[RETURN_ENABLED_PARAM].getValue() : 0.0f;
    }

    t += args.sampleTime;
  }

  void onRemove() override {
    fileSaver.wait();
  }
};
