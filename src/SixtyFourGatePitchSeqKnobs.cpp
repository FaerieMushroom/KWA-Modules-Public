#include "SixtyFourGatePitchSeqKnobs.hpp"

// How much an inactive step's knob and button are washed out
static const float SHADE_KNOB_MUTED = 0.55f;        // inside the step count, trigger muted
static const float SHADE_KNOB_OUT_OF_RANGE = 0.93f; // past the step count
static const float SHADE_BUTTON_MUTED = 0.78f;      // darker, the button is much smaller
static const float SHADE_BUTTON_OUT_OF_RANGE = 0.93f;

////////////////
//// MODULE
////////////////

/** A step knob's value. While the display is on Probability the knob is a chance of the
step firing, so it is shown and typed as a percentage instead of as volts. */
struct StepKnobQuantity : engine::ParamQuantity {
	SixtyFourGatePitchSeqKnobs* getSeq() {
		return dynamic_cast<SixtyFourGatePitchSeqKnobs*>(this->module);
	}
	bool isProbability() {
		SixtyFourGatePitchSeqKnobs* seq = getSeq();
		return seq && seq->getDataSource() == 1;
	}
	/** Stored on the usual 0 to 10 scale, which is 0% to 100% */
	float getPercent() {
		return math::clamp(getValue() * 10.f, 0.f, 100.f);
	}
	std::string getDisplayValueString() override {
		if (!isProbability())
			return engine::ParamQuantity::getDisplayValueString();
		return string::f("%.0f %%", getPercent());
	}
	std::string getString() override {
		if (!isProbability() || name == "")
			return engine::ParamQuantity::getString();
		return string::f("%s %s", name.c_str(), getDisplayValueString().c_str());
	}
	void setDisplayValueString(std::string s) override {
		if (!isProbability()) {
			engine::ParamQuantity::setDisplayValueString(s);
			return;
		}
		// Accepts "80" or "80%"
		while (!s.empty() && (s.back() == '%' || s.back() == ' '))
			s.pop_back();
		setValue(std::round(std::atof(s.c_str()) * 0.1f));
	}
};

SixtyFourGatePitchSeqKnobs::SixtyFourGatePitchSeqKnobs() {
	//knobs
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	// Every step fires unless a knob says otherwise
	std::fill(&probabilityPCV[0][0], &probabilityPCV[0][0] + (10 * 64), 10.f);
	// Every step starts enabled with a note slot ready, so the sequencer runs out of the box
	std::fill(&gatePCV[0][0], &gatePCV[0][0] + (10 * 64), 10.f);
	std::fill(&hasRecordedDataPCV[0][0], &hasRecordedDataPCV[0][0] + (10 * 64), 10.f);
	for (int i = 0; i < 64; i++) {
		StepKnobQuantity* q = configParam<StepKnobQuantity>(STEP_KNOB_PARAMS + i, -10.f, 10.f, 0.f, string::f("Step %d V/Oct", i + 1), "V");
		// Rack smooths a param towards a target and getValue() reports the target, while
		// getImmediateValue() reports the interpolated value. With 64 knobs sharing one
		// smoothing slot that reads back as noise, and feeding it back into the step data
		// would drag stored values around. Keep these exact.
		q->smoothEnabled = false;
		//buttons - the middle of each knob
		configButton(STEP_BUTTON_PARAMS + i, string::f("Step %d", i + 1));
	}
	configButton(RECORD_MODE_PARAM, "Record");
	configButton(EDIT_MODE_PARAM, "Edit Mode");
	configSwitch(GATE_MODE_PARAM, 0.f, 10.f, 10.f, "Gate Mode");

	//knobs
	configSwitch(DISPLAY_MODE_PARAM, 1.f, 5.f, 2.f, "Display Mode", {"Probability", "V/Oct", "Velocity", "Data 1", "Data 2"});
	getParamQuantity(DISPLAY_MODE_PARAM)->snapEnabled = true;
	configParam(STEP_COUNT_PARAM, 1.f, 64.f, 8.f, "Steps");
	getParamQuantity(STEP_COUNT_PARAM)->snapEnabled = true;
	//Right click the module to switch the step knobs between bipolar and unipolar
	configSwitch(KNOB_RANGE_PARAM, 0.f, 1.f, 0.f, "Knob Range", {"-10 to 10", "0 to 10"});
	getParamQuantity(KNOB_RANGE_PARAM)->randomizeEnabled = false;

	//inputs
	configInput(CLOCK_INPUT, "Clock");
	configInput(RESET_INPUT, "Reset");
	configInput(GATE_INPUT, "Gate");
	configInput(VOCT_INPUT, "V/Oct");
	configInput(VELOCITY_INPUT, "Velocity");

	//outputs
	configOutput(CLOCK_OUTPUT, "Clock");
	configOutput(RESET_OUTPUT, "Reset");
	configOutput(EOC_OUTPUT, "EOC");

	configOutput(GATE_OUTPUT, "Gate");
	configOutput(VOCT_OUTPUT, "V/Oct");
	configOutput(VELOCITY_OUTPUT, "Velocity");

	leftExpander.producerMessage = leftMessages[0];
	leftExpander.consumerMessage = leftMessages[1];
	rightExpander.producerMessage = rightMessages[0];
	rightExpander.consumerMessage = rightMessages[1];

	// Start in Gates mode, so the panel and the behaviour agree and clicking the middle
	// of a knob marks a trigger straight away
	params[GATE_MODE_PARAM].setValue(10.f);
	gateModeSelected = true;
	setModeBrightnesses();
}

int SixtyFourGatePitchSeqKnobs::getDataSource() {
	return (int)std::roundf(params[DISPLAY_MODE_PARAM].getValue());
}

float SixtyFourGatePitchSeqKnobs::getKnobMin() {
	return params[KNOB_RANGE_PARAM].getValue() > 0.5f ? 0.f : -10.f;
}

float SixtyFourGatePitchSeqKnobs::knobRangeMin() {
	// Probability is a chance, so it is always 0 to 10 whatever the knob range is set to
	if (getDataSource() == 1)
		return 0.f;
	return getKnobMin();
}

float* SixtyFourGatePitchSeqKnobs::stepValuePtr(int i) {
	switch (getDataSource()) {
		case 1: return &probabilityPCV[sequencePage][i];
		case 3: return &velocityPCV[sequencePage][i];
		case 4: return &dataOnePCV[sequencePage][i];
		case 5: return &dataTwoPCV[sequencePage][i];
		default: return &voctPCV[sequencePage][i];
	}
}

bool SixtyFourGatePitchSeqKnobs::rollTrigger(int step) {
	const float chance = math::clamp(probabilityPCV[sequencePage][step] * 0.1f, 0.f, 1.f);
	return random::uniform() < chance;
}

bool SixtyFourGatePitchSeqKnobs::stepFires(int step) {
	if (step < 0 || step >= 64)
		return false;
	if (engine.rolledStep != step) {
		engine.rolledStep = step;
		// A step the user clicked off never fires, whatever its probability says
		engine.rolledResult = gatePCV[sequencePage][step] > 0.1f && rollTrigger(step);
	}
	return engine.rolledResult;
}

void SixtyFourGatePitchSeqKnobs::invalidateStepRolls() {
	engine.rolledStep = -1;
}

void SixtyFourGatePitchSeqKnobs::syncKnob(int i) {
	if (i < 0 || i >= 64)
		return;
	ParamQuantity* quantity = getParamQuantity(STEP_KNOB_PARAMS + i);
	const float lo = knobRangeMin();
	quantity->minValue = lo;
	quantity->maxValue = 10.f;
	quantity->setImmediateValue(math::clamp(*stepValuePtr(i), lo, 10.f));
	knobEcho[i] = quantity->getImmediateValue();
}

void SixtyFourGatePitchSeqKnobs::updateKnobLabels() {
	const int mode = getDataSource();
	const char* what = "Probability";
	if (mode == 2) what = "V/Oct";
	else if (mode == 3) what = "Velocity";
	else if (mode == 4) what = "Data 1";
	else if (mode == 5) what = "Data 2";
	for (int i = 0; i < 64; i++) {
		ParamQuantity* quantity = getParamQuantity(STEP_KNOB_PARAMS + i);
		quantity->name = string::f("Step %d %s", i + 1, what);
	}
}

void SixtyFourGatePitchSeqKnobs::syncKnobs() {
	updateKnobLabels();
	for (int i = 0; i < 64; i++) {
		syncKnob(i);
	}
}

void SixtyFourGatePitchSeqKnobs::pullKnobs() {
	const float lo = knobRangeMin();
	for (int i = 0; i < 64; i++) {
		ParamQuantity* quantity = getParamQuantity(STEP_KNOB_PARAMS + i);
		// Immediate value, so what is stored on the step is exactly what the knob is on
		const float knobValue = quantity->getImmediateValue();
		if (knobValue == knobEcho[i])
			continue;
		// The knob was turned, so write it back into the step it addresses
		knobEcho[i] = knobValue;
		*stepValuePtr(i) = math::clamp(knobValue, lo, 10.f);
		// A turned probability knob has to take effect on the next visit, not the one after
		invalidateStepRolls();
		shouldRefreshDisplay = true;
	}
}

void SixtyFourGatePitchSeqKnobs::process(const ProcessArgs& args) {
	// Mono by design. A knob sequencer steps as one voice, so polyphony would only carry
	// sixteen copies of data nothing reads. Gate channels past the first are ignored.
	//less often process - for sample insensitive code
	if (processCounter >= 49) {
		processFifty(args);
		processCounter = 0;
	}
	processCounter++;
	outputs[GATE_OUTPUT].setChannels(1);
	outputs[VOCT_OUTPUT].setChannels(1);
	outputs[VELOCITY_OUTPUT].setChannels(1);

	// Read left expander messages
	const bool is_baby = leftExpander.module && (leftExpander.module->model == modelSixtyFourGatePitchSeqExpander);
	if (is_baby) {
		float* leftMessage = (float*)leftExpander.consumerMessage;
		//[0] - Play
		expanderSignalPlay = leftMessage[0];
		//[1] - Page
		expanderSignalPage = leftMessage[1];
		//[2] - Playhead
		expanderSignalPlayhead = leftMessage[2];
		//[3] - Data One
		expanderSignalDataOne = leftMessage[3];
		//[4] - Data Two
		expanderSignalDataTwo = leftMessage[4];
		//[5] - Steps
		expanderSignalSteps = leftMessage[5];
		//[6] - Copy
		expanderSignalCopy = leftMessage[6];
		//[7] - Cut
		expanderSignalCut = leftMessage[7];
		//[8] - Paste
		expanderSignalPaste = leftMessage[8];
		//[9] - One Shot
		expanderSignalOneShot = leftMessage[9];
	}
	// Send left expander messages
	if (is_baby) {
		float* leftSendMessage = (float*)leftExpander.module->rightExpander.producerMessage;
		// Data one and two of the step that is playing, so the expander's data outs are live
		const int sentStep = currentStep < 64 ? currentStep : 0;
		leftSendMessage[0] = dataOnePCV[sequencePage][sentStep];
		leftSendMessage[1] = dataTwoPCV[sequencePage][sentStep];
		// Flip messages at the end of the timestep
		leftExpander.module->rightExpander.messageFlipRequested = true;
	}

	const int previousPage = sequencePage;
	sequencePage = std::roundf(is_baby ? (expanderSignalPage - 1) : 0);
	sequencePage = sequencePage < 0 ? 0 : sequencePage;
	sequencePage = sequencePage > 9 ? 9 : sequencePage;
	if (sequencePage != previousPage) {
		// The knobs mirror the page, so they have to follow it
		syncKnobs();
		shouldRefreshDisplay = true;
	}
	if(is_baby){
		stepCountInt = expanderSignalSteps > -0.5f ? std::roundf(expanderSignalSteps * 6.4) : params[STEP_COUNT_PARAM].getValue();
		stepCountInt = stepCountInt < 1 ? 1 : stepCountInt;
		stepCountInt = stepCountInt > 64 ? 64 : stepCountInt;
	} else{
		stepCountInt = params[STEP_COUNT_PARAM].getValue();
	}

	// Playback other voltages
	if (currentStep < 64) {
		if(hasRecordedDataPCV[sequencePage][currentStep] > 0 && !(inputs[GATE_INPUT].getVoltage() > 0)){
			outputs[VOCT_OUTPUT].setVoltage(voctPCV[sequencePage][currentStep]);
			outputs[VELOCITY_OUTPUT].setVoltage(velocityPCV[sequencePage][currentStep]);
		}
	}

	// Gate Rise + Fall
	engine.gateFellThisSample = false;
	engine.gateRoseThisSample = false;
	float gateVoltage = inputs[GATE_INPUT].getVoltage();
			// HIGH to LOW
		if (engine.isGateHigh) {
			if (gateVoltage <= 0.1f) {
				engine.isGateHigh = false;
				engine.gateFellThisSample = true;
			}
		}
		else {
			// LOW to HIGH
			if (gateVoltage >= 2.f) {
				engine.isGateHigh = true;
				engine.gateRoseThisSample = true;
			}
		}

	// Voct Movement Detection
	engine.voctMovedThisSample = false;
	float newVoctVoltage = inputs[VOCT_INPUT].getVoltage();
	if ((newVoctVoltage != engine.voctVoltage) && engine.isGateHigh){
		engine.voctMovedThisSample = true;
		engine.voctVoltage = newVoctVoltage;
	}

	// Reset - Schmitt trigger
	bool resetTriggeredInternally = false;
	if (resetInputSchmitt.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
		const int playheadModeInt = std::floor(expanderSignalPlayhead);
		if (playheadModeInt == 1) { // Descend
			currentStep = stepCountInt - 1;
		} else {
			currentStep = 0;
		}

		if (playheadModeInt == 2) { // Ping Pong
			pingPongDir = 1;
		}

		currentSubstep = 0;
		shouldRefreshDisplay = true;
		resetTriggeredInternally = true;
		// Pulse outputs if gate on reset step
		currentStepFired = true;
		engine.hasTriggerOutputPulsedThisStep = false;
		if (currentStep < 64) {
			engine.hasTriggerOutputPulsedThisStep = true;
			if (stepFires(currentStep))
				engine.triggerOutputPulse.trigger(1e-3f);
			else
				currentStepFired = false;
		}
	}

	// Clock
	if (!resetTriggeredInternally && clockInputSchmitt.process(inputs[CLOCK_INPUT].getVoltage(),0.1f, 1.f)) {
		currentSubstep += 1;
		// Don't Increment edit steps if its -1 to prevent random deselecting of edit selection
		if (clockStepsSinceLastEdit > -1){
			clockStepsSinceLastEdit++;
			//Clear edit selection after waiting for 2 substeps since edit was entered
			if(clockStepsSinceLastEdit > 2){
				int selectedCount = 0;
				int lastSelectedIndex = 0;
				// Clear edit selection
				for (int i = 0; i < 64; i++) {
					if(editModeSelectedGates[i]){
						editModeSelectedGates[i] = false;
						selectedCount++;
						lastSelectedIndex = i;
					}
				}
				// only 1 gate was edited, step to next
				if(selectedCount == 1){
					editModeSelectedGates[lastSelectedIndex >= (stepCountInt - 1) ? 0 : lastSelectedIndex + 1] = true;
				}
				// Reset editCount to allow clearing of note params on next input
				editCount = 0;
				// Sleep the last edit clock
				clockStepsSinceLastEdit = -1;
			}
		}
		//Substep loop
		const int playheadModeInt = std::floor(expanderSignalPlayhead);
		if (currentSubstep >= 4){
			engine.hasTriggerOutputPulsedThisStep = false;
			if(is_baby ? expanderSignalPlay > 0.f : true) {
				switch(playheadModeInt){
					default:
						if (currentStep >= stepCountInt - 1) {
							if (currentStep < 64) {
								if (expanderSignalOneShot > 0.f) {
									currentStep = 64; // Stop
								} else {
									currentStep = 0;
								}
								eocOutputPulse.trigger(1e-3f);
							}
						}
						else if (currentStep < 64) {
							currentStep += 1;
						}
						break;
					case 1:
						if (currentStep < 64 && currentStep > stepCountInt - 1) {
							//prevent being above max steps
							currentStep = stepCountInt - 1;
						}
						if (currentStep < 64 && currentStep <= 0) {
							if (expanderSignalOneShot > 0.f) {
								currentStep = 64; // Stop
							} else {
								currentStep = stepCountInt - 1;
							}
							eocOutputPulse.trigger(1e-3f);
						}
						else if (currentStep < 64) {
							currentStep -= 1;
						}
						break;
					case 2:
						if(pingPongDir == 1){
							if (currentStep >= stepCountInt - 1) {
								if (currentStep < 64) {
									if (stepCountInt > 1) {
										currentStep -= 1;
										pingPongDir = 0;
									} else {
										pingPongDir = 0;
										eocOutputPulse.trigger(1e-3f);
									}
								}
							}
							else if (currentStep < 64) {
								currentStep += 1;
							}
						} else {
							if (currentStep < 64 && currentStep > stepCountInt - 1) {
								//prevent being above max steps
								currentStep = stepCountInt - 1;
							}
							if (currentStep < 64 && currentStep <= 0) {
								if (expanderSignalOneShot > 0.f) {
									currentStep = 64; // Stop
									pingPongDir = 1;
								} else {
									if (stepCountInt > 1) {
										currentStep = 1;
										pingPongDir = 1;
									} else {
										currentStep = 0;
										pingPongDir = 1;
									}
								}
								eocOutputPulse.trigger(1e-3f);
} else if (currentStep < 64) {
							currentStep -= 1;
						}
					}
					break;
				case 3: {
					// Random - a different step inside the range every clock, never the one
					// that is already playing
					if (currentStep < 64) {
						const int range = std::max(1, std::min(stepCountInt, 64));
						if (expanderSignalOneShot > 0.f) {
							// Random has no cycle to run through, so one shot plays one note
							currentStep = 64; // Stop
						} else if (range > 1) {
							// Offset in 1..range-1, so the landing step always differs
							const int offset = 1 + (int)(random::uniform() * (range - 1));
							currentStep = (currentStep + offset) % range;
						} else {
							// Only one step to play, and it is 0
							currentStep = 0;
						}
					}
					break;
				}
			}
				//Pulse outputs if gate
				currentStepFired = true;
				if (currentStep < 64) {
					if(!engine.hasTriggerOutputPulsedThisStep){
						engine.hasTriggerOutputPulsedThisStep = true;
						// Rolled once per step, then reused by the data write further down
						if(stepFires(currentStep))
							engine.triggerOutputPulse.trigger(1e-3f);
						else
							currentStepFired = false;
					}
				}
				currentSubstep = 0;
			}
			clockOutputPulse.trigger(1e-3f);
		}
		// Quantize substep to a working step
		if (currentStep < 64) {
			switch(playheadModeInt){
				default:
					if (currentSubstep >= 3){
						currentWorkingStep = (currentStep == stepCountInt-1) ? 0 : currentStep + 1;
						// Reset edit count to clear notes when recording
						if(isRecording){
							editCount = 0;
						}
					} else {
						currentWorkingStep = currentStep;
					}
					break;
				case 1:
					if (currentSubstep >= 3) {
						if (currentStep <= 0) {
							currentWorkingStep = (expanderSignalOneShot > 0.f) ? 64 : stepCountInt - 1;
						} else {
							currentWorkingStep = currentStep - 1;
						}
						// Reset edit count to clear notes when recording
						if(isRecording){
							editCount = 0;
						}
					} else {
						currentWorkingStep = currentStep;
					}
					break;
				case 2:
					if(pingPongDir == 1){
						if (currentSubstep >= 3){
							currentWorkingStep = (currentStep >= stepCountInt-1) ? std::max(0, currentStep - 1) : currentStep + 1;
							// Reset edit count to clear notes when recording
							if(isRecording){
								editCount = 0;
							}
						} else {
							currentWorkingStep = currentStep;
						}
					} else {
						if (currentSubstep >= 3){
							if (currentStep <= 0) {
								if (expanderSignalOneShot > 0.f) {
									currentWorkingStep = 64;
								} else {
									currentWorkingStep = std::min(stepCountInt - 1, 1);
								}
							} else {
								currentWorkingStep = currentStep - 1;
							}
							// Reset edit count to clear notes when recording
							if(isRecording){
								editCount = 0;
							}
						} else {
							currentWorkingStep = currentStep;
						}
					}
					break;
				case 3:
					// Random has no predictable next step, so the gate writes onto the step
					// that is playing rather than ahead of it
					currentWorkingStep = currentStep;
					break;
			}
		}
		shouldRefreshDisplay = true;
	}

	// On input gate rise. Only a step that is enabled and actually fired takes data,
	// so a step the user clicked off, or one that lost its probability roll, keeps
	// exactly what it already had. When recordStep is the step that just fired this
	// reuses that same roll, so a note can never fire without landing.
	const int recordStep = currentWorkingStep < 64 ? currentWorkingStep : 0;
	if ((engine.gateRoseThisSample || engine.voctMovedThisSample)
		&& currentStep < 64
		&& stepFires(recordStep))
	{
		// recording
		if(isRecording)
		{
			if(editCount == 0) {
				clearNoteParams(recordStep);
			}
			// Increment edit count, To reset note params only once per step
			editCount++;

			// Record Data
			gatePCV[sequencePage][recordStep] = 10.f;
			hasRecordedDataPCV[sequencePage][recordStep] = 10.f;
			voctPCV[sequencePage][recordStep] = inputs[VOCT_INPUT].getVoltage();
			velocityPCV[sequencePage][recordStep] = inputs[VELOCITY_INPUT].getVoltage();

			// The matching knob follows what was just recorded
			syncKnob(recordStep);

		// in edit mode
		} else if (!gateModeSelected)
		{
			// Iterate the step knobs
			for (int i = 0; i < 64; i++) {
				// If step is selected with edit mode
				if(editModeSelectedGates[i]){
					if(editCount == 0) {
						// Subloop neccesary because edit count increments
						for (int j = 0; j < 64; j++) {
							if(editModeSelectedGates[j]){
								clearNoteParams(j);
							}
						}
					}
					// Edit Data
					gatePCV[sequencePage][i] = 10.f;
					hasRecordedDataPCV[sequencePage][i] = 10.f;
					voctPCV[sequencePage][i] = inputs[VOCT_INPUT].getVoltage();
					velocityPCV[sequencePage][i] = inputs[VELOCITY_INPUT].getVoltage();
					// Increment edit count, To reset note params only once per edit
					editCount++;
					syncKnob(i);
				}
			}
			// Start counting up clock steps again
			clockStepsSinceLastEdit = 0;
		}
	}

	if(engine.gateFellThisSample){
		outputs[GATE_OUTPUT].setVoltage(0.f);
	}

	// Output - pulses
	outputs[GATE_OUTPUT].setVoltage((engine.triggerOutputPulse.process(args.sampleTime) ? 10.f : 0.f) > inputs[GATE_INPUT].getVoltage() ? 10.f : inputs[GATE_INPUT].getVoltage());
	outputs[RESET_OUTPUT].setVoltage(inputs[RESET_INPUT].getVoltage());
	outputs[EOC_OUTPUT].setVoltage(eocOutputPulse.process(args.sampleTime) ? 10.f : 0.f);
	outputs[CLOCK_OUTPUT].setVoltage(clockOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

}

void SixtyFourGatePitchSeqKnobs::processFifty(const ProcessArgs& args){
	if(!wasInitialized){
		params[GATE_MODE_PARAM].setValue(10.f);
		wasInitialized = true;
		// Roll step 0 up front so the playhead is right before the clock ever runs
		currentStepFired = stepFires(currentStep);
		syncKnobs();
		shouldRefreshDisplay = true;
	}

	// [MODE 1] - Record mode button pressed:
	if(params[RECORD_MODE_PARAM].getValue() > 0.f) {
			isRecording = !isRecording;
			gateModeSelected = true;
			setModeBrightnesses();
			params[GATE_MODE_PARAM].setValue(true);
			params[RECORD_MODE_PARAM].setValue(false);
	}

	// [MODE 2] - Edit mode button pressed:
	if(params[EDIT_MODE_PARAM].getValue() > 0.f) {
		for (int i = 0; i < 64; i++) {
			editModeSelectedGates[i] = false;
		}
		isRecording = false;
		gateModeSelected = false;
		setModeBrightnesses();
		params[EDIT_MODE_PARAM].setValue(false);
	}

	// [MODE 3] - Gate mode button pressed:
	if(params[GATE_MODE_PARAM].getValue() > 0.f){
		gateModeSelected = true;
		setModeBrightnesses();
		params[GATE_MODE_PARAM].setValue(false);
	}

	// The display knob decides what the step knobs address
	const int displayMode = getDataSource();
	if (displayMode != lastDisplayMode) {
		lastDisplayMode = displayMode;
		syncKnobs();
		shouldRefreshDisplay = true;
	}
	// Context menu knob range
	const int rangeMode = params[KNOB_RANGE_PARAM].getValue() > 0.5f ? 1 : 0;
	if (rangeMode != knobRangeMode) {
		knobRangeMode = rangeMode;
		syncKnobs();
		shouldRefreshDisplay = true;
	}

	if(expanderSignalCopy > 0.1f){
		if(!copyPressedDown){
			//DEBUG("Copy pressed down");
			copyPressedDown = true;
			copyNotes(false);
		}
	} else {
		copyPressedDown = false;
	}
	if(expanderSignalCut > 0.1f){
		if(!cutPressedDown){
			//DEBUG("Cut pressed down");
			cutPressedDown = true;
			cutNotes();
		}
	} else {
		cutPressedDown = false;
	}
	if(expanderSignalPaste > 0.1f){
		if(!pastePressedDown){
			//DEBUG("Paste pressed down");
			pastePressedDown = true;
			pasteNotes();
		}
	} else {
		pastePressedDown = false;
	}

	// Iterate the step buttons, same press handling as the KWA Pitch 64
	bool anyThisSet = false;
	for (int i = 0; i < 64; i++) {
		if (params[STEP_BUTTON_PARAMS + i].getValue() > 0.f) {
			anyThisSet = true;
			wasButtonPressedThisSample = true;
			shouldRefreshDisplay = true;
			if (!gateModifiedSinceRelease) {
				// Gates mode - mute or unmute this step only. Muting keeps the note data,
				// so unmuting brings back exactly what was there before.
				if (gateModeSelected) {
					if (gatePCV[sequencePage][i] > 0.1f) {
						gatePCV[sequencePage][i] = 0.f;
					} else {
						gatePCV[sequencePage][i] = 10.f;
						hasRecordedDataPCV[sequencePage][i] = 10.f;
					}
				}
				// Edit mode - select or deselect this step only
				else {
					if (!anyPressed) {
						editModeSelectedGates[i] = !editModeSelectedGates[i];
						anyPressed = true;
					}
				}
				// The gate just changed, so a cached roll for that step is stale
				invalidateStepRolls();
				gateModifiedSinceRelease = true;
			}
		}
	}
	if (!anyThisSet) {
		anyPressed = false;
		gateModifiedSinceRelease = false;
	}

	// Turned knobs back into step values
	pullKnobs();

	//Display - update when a button is clicked or during a clock step
	if(shouldRefreshDisplay || wasButtonPressedThisSample){
		updateDisplay();
	}

	//Audition
	if(inputs[GATE_INPUT].getVoltage() > 0.f){
		outputs[GATE_OUTPUT].setVoltage(inputs[GATE_INPUT].getVoltage());
		outputs[VOCT_OUTPUT].setVoltage(inputs[VOCT_INPUT].getVoltage());
		outputs[VELOCITY_OUTPUT].setVoltage(inputs[VELOCITY_INPUT].getVoltage());
	}
}

void SixtyFourGatePitchSeqKnobs::copyNotes(bool cut) {
	for(int i = 0; i < 64; i++){
		if(editModeSelectedGates[i]){
			gateCopy = gatePCV[sequencePage][i] > 0.1f;
			voctCopy = voctPCV[sequencePage][i];
			velocityCopy = velocityPCV[sequencePage][i];
			hasRecordedDataCopy = hasRecordedDataPCV[sequencePage][i] > 0.1f;
			dataOneCopy = dataOnePCV[sequencePage][i];
			dataTwoCopy = dataTwoPCV[sequencePage][i];
			selectionCopy = true;
			//todo multi select, require first selected index, paste transforms and loops at max step count
		}
	}
	for (int i = 0; i < 64; i++) {
		if(cut){
			if(editModeSelectedGates[i]){
				clearNoteParams(i);
			}
		}
		editModeSelectedGates[i] = false;
	}
	shouldRefreshDisplay = true;
}

void SixtyFourGatePitchSeqKnobs::cutNotes() {
	copyNotes(true);
}

void SixtyFourGatePitchSeqKnobs::pasteNotes() {
	for(int i = 0; i < 64; i++){
		if(editModeSelectedGates[i]){
			gatePCV[sequencePage][i] = gateCopy ? 10.f : 0.f;
			voctPCV[sequencePage][i] = voctCopy;
			velocityPCV[sequencePage][i] = velocityCopy;
			hasRecordedDataPCV[sequencePage][i] = hasRecordedDataCopy ? 10.f : 0.f;
			dataOnePCV[sequencePage][i] = dataOneCopy;
			dataTwoPCV[sequencePage][i] = dataTwoCopy;
		}
	}
	for (int i = 0; i < 64; i++) {
		editModeSelectedGates[i] = false;
	}
	syncKnobs();
	shouldRefreshDisplay = true;
	invalidateStepRolls();
}

void SixtyFourGatePitchSeqKnobs::setModeBrightnesses() {
	// Set Mode Brightnesses
	lights[RECORD_MODE_LIGHT].setBrightness(isRecording);
	lights[EDIT_MODE_LIGHT].setBrightness(!gateModeSelected);
	lights[GATE_MODE_LIGHT].setBrightness(gateModeSelected);
}

void SixtyFourGatePitchSeqKnobs::clearNoteParams(int i) {
	gatePCV[sequencePage][i] = 0.f;
	velocityPCV[sequencePage][i] = 0.f;
	voctPCV[sequencePage][i] = 0.f;
	hasRecordedDataPCV[sequencePage][i] = 0.f;
	dataOnePCV[sequencePage][i] = 0.f;
	dataTwoPCV[sequencePage][i] = 0.f;
}

void SixtyFourGatePitchSeqKnobs::updateDisplay(){
	float red = 0;
	float green = 0;
	float blue = 0;

	const int displayMode = getDataSource();
	// Colours are absolute: the value is mapped straight onto the knob's own range, so
	// a given value always looks the same no matter what else is on the matrix. The
	// knob range only scales the colour, it never rescales the knob position.
	const float rangeLo = knobRangeMin();
	const float rangeSpan = 10.f - rangeLo;

	for (int i = 0; i < 64; i++) {
		const float gateVal = gatePCV[sequencePage][i] > 0.1f ? 10.f : -10.f;
		const bool written = hasRecordedDataPCV[sequencePage][i] > 0.1f;
		// The same value the knob is showing, so the colour and the pointer always agree
		const float value = *stepValuePtr(i);
		// Where it sits on the knob's own range, 0 at the bottom and 1 at the top
		const float t = math::clamp((value - rangeLo) / rangeSpan, 0.f, 1.f);
		//Display Mode Brightness
		if(i <= (stepCountInt - 1)) {
			switch(displayMode){
				//[1] - Probability display, blue brightness is the chance of firing
				case 1:
					blue = t;
					break;

				//[2] - Voct display, red at the bottom of the range to green at the top
				case 2:
					red = 1.f - t;
					green = t;
					//blue should be blank unless no voct then we do dull blue (10%) to show that theres still a gate there
					if (!written) {
						blue = gateVal / 100.f;
					}
					break;

				//[3] - Velocity display, dim red to bright red
				case 3:
					red = t < .05f ? .05f : t;
					if (!written) {
						blue = gateVal / 100.f;
					}
					break;

				//[4] - Data one display, cyan
				case 4:
					green = t;
					if (written) {
						blue = t;
					} else {
						green = 0.f;
						blue = gateVal / 100.f;
					}
					break;

				//[5] - Data two display, magenta
				case 5:
				default:
					red = t;
					if (written) {
						blue = t;
					} else {
						red = 0.f;
						blue = gateVal / 100.f;
					}
					break;
			}

			// A muted step is dark. Its knob and button still show the stored value
			if (gateVal <= 0.1f) {
				red = 0.f;
				green = 0.f;
				blue = 0.f;
				// Muted but still inside the step count, so a softer wash than a step
				// that is out of range
				knobShade[i] = SHADE_KNOB_MUTED;
				buttonShade[i] = SHADE_BUTTON_MUTED;
			} else {
				knobShade[i] = 0.f;
				buttonShade[i] = 0.f;
			}
			if (!gateModeSelected && editModeSelectedGates[i]) {
				//in edit mode, light up selected gates
				red = 10.f;
				green = 10.f;
				blue = 10.f;
			}
			lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(red);
			lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(green);
			lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(blue);
		}else{
			//if over step count, dont display
			lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(0.f);
			lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(0.f);
			lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(0.f);
			knobShade[i] = SHADE_KNOB_OUT_OF_RANGE;
			buttonShade[i] = SHADE_BUTTON_OUT_OF_RANGE;
		}

		// Active step - green glow behind the button, and only while the step actually
		// fired, so a step that lost its probability roll stays dark
		lights[PLAYHEAD_LIGHTS + i].setBrightness(i == currentStep && currentStepFired ? 1.f : 0.f);
	}
	//reset vars so we dont iterate every sample
	wasButtonPressedThisSample = false;
	shouldRefreshDisplay = false;
}

json_t* SixtyFourGatePitchSeqKnobs::dataToJson() {
	// PCV Dimensions
    const int pD = 10;
    const int vD = 64;

	json_t* rootJ = json_object(); // Create a JSON object

	// One voice, so the shape is page then step
	json_t* gateArr = json_array();
	json_t* voctArr = json_array();
	json_t* recArr = json_array();
	json_t* velArr = json_array();
	json_t* dataOneArr = json_array();
	json_t* dataTwoArr = json_array();
	json_t* probArr = json_array();
	for (int p = 0; p < pD; p++) {
		json_t* gatePage = json_array();
		json_t* voctPage = json_array();
		json_t* recPage = json_array();
		json_t* velPage = json_array();
		json_t* dataOnePage = json_array();
		json_t* dataTwoPage = json_array();
		json_t* probPage = json_array();

		for (int v = 0; v < vD; v++) {
			json_array_append_new(gatePage, json_real(gatePCV[p][v]));
			json_array_append_new(voctPage, json_real(voctPCV[p][v]));
			json_array_append_new(recPage, json_real(hasRecordedDataPCV[p][v]));
			json_array_append_new(velPage, json_real(velocityPCV[p][v]));
			json_array_append_new(dataOnePage, json_real(dataOnePCV[p][v]));
			json_array_append_new(dataTwoPage, json_real(dataTwoPCV[p][v]));
			json_array_append_new(probPage, json_real(probabilityPCV[p][v]));
		}
		json_array_append_new(gateArr, gatePage);
		json_array_append_new(voctArr, voctPage);
		json_array_append_new(recArr, recPage);
		json_array_append_new(velArr, velPage);
		json_array_append_new(dataOneArr, dataOnePage);
		json_array_append_new(dataTwoArr, dataTwoPage);
		json_array_append_new(probArr, probPage);
	}
	json_object_set_new(rootJ, "gatePCV", gateArr);
	json_object_set_new(rootJ, "voctPCV", voctArr);
	json_object_set_new(rootJ, "hasRecordedDataPCV", recArr);
	json_object_set_new(rootJ, "velocityPCV", velArr);
	json_object_set_new(rootJ, "dataOnePCV", dataOneArr);
	json_object_set_new(rootJ, "dataTwoPCV", dataTwoArr);
	json_object_set_new(rootJ, "probabilityPCV", probArr);

	return rootJ;
}

void SixtyFourGatePitchSeqKnobs::dataFromJson(json_t* rootJ) {
	// PCV Dimensions
    const int pD = 10;
    const int vD = 64;

	json_t* gateArr = json_object_get(rootJ, "gatePCV");
	json_t* voctArr = json_object_get(rootJ, "voctPCV");
	json_t* recArr = json_object_get(rootJ, "hasRecordedDataPCV");
	json_t* velArr = json_object_get(rootJ, "velocityPCV");
	json_t* dataOneArr = json_object_get(rootJ, "dataOnePCV");
	json_t* dataTwoArr = json_object_get(rootJ, "dataTwoPCV");
	// Optional, patches saved before probability existed keep the default of always
	json_t* probArr = json_object_get(rootJ, "probabilityPCV");
    if (gateArr && json_is_array(gateArr) &&
		voctArr && json_is_array(voctArr) &&
		recArr && json_is_array(recArr) &&
		velArr && json_is_array(velArr) &&
		dataOneArr && json_is_array(dataOneArr) &&
		dataTwoArr && json_is_array(dataTwoArr)) {
        for (int p = 0; p < pD; p++) {
            json_t* gatePage = json_array_get(gateArr, p);
			json_t* voctPage = json_array_get(voctArr, p);
			json_t* recPage = json_array_get(recArr, p);
			json_t* velPage = json_array_get(velArr, p);
			json_t* dataOnePage = json_array_get(dataOneArr, p);
			json_t* dataTwoPage = json_array_get(dataTwoArr, p);
			json_t* probPage = probArr ? json_array_get(probArr, p) : NULL;
            if (gatePage && json_is_array(gatePage) &&
				voctPage && json_is_array(voctPage) &&
				recPage && json_is_array(recPage) &&
				velPage && json_is_array(velPage) &&
				dataOnePage && json_is_array(dataOnePage) &&
				dataTwoPage && json_is_array(dataTwoPage)) {
                for (int v = 0; v < vD; v++) {
                    json_t* gateValue = json_array_get(gatePage, v);
                    if (gateValue && json_is_real(gateValue)) {
                        gatePCV[p][v] = json_real_value(gateValue);
                    }
					json_t* voctValue = json_array_get(voctPage, v);
                    if (voctValue && json_is_real(voctValue)) {
                        voctPCV[p][v] = json_real_value(voctValue);
                    }
					json_t* recValue = json_array_get(recPage, v);
                    if (recValue && json_is_real(recValue)) {
                        hasRecordedDataPCV[p][v] = json_real_value(recValue);
                    }
					json_t* velValue = json_array_get(velPage, v);
                    if (velValue && json_is_real(velValue)) {
                        velocityPCV[p][v] = json_real_value(velValue);
                    }
					json_t* dataOneValue = json_array_get(dataOnePage, v);
                    if (dataOneValue && json_is_real(dataOneValue)) {
                        dataOnePCV[p][v] = json_real_value(dataOneValue);
                    }
					json_t* dataTwoValue = json_array_get(dataTwoPage, v);
                    if (dataTwoValue && json_is_real(dataTwoValue)) {
                        dataTwoPCV[p][v] = json_real_value(dataTwoValue);
                    }
					json_t* probValue = probPage ? json_array_get(probPage, v) : NULL;
                    if (probValue && json_is_real(probValue)) {
                        probabilityPCV[p][v] = json_real_value(probValue);
                    }
                }
            }
        }
	}
	// The knobs mirror the stored values, so pull them back in
	syncKnobs();
	shouldRefreshDisplay = true;
	invalidateStepRolls();
}

void SixtyFourGatePitchSeqKnobs::onRandomize(const RandomizeEvent& e){
	Module::onRandomize(e);
	Module::onRandomize(e);
	const int pD = 10;
    const int vD = 64;
	for (int p = 0; p < pD; p++) {
        for (int v = 0; v < vD; v++) {
				hasRecordedDataPCV[p][v] = 10.f;
				gatePCV[p][v] = 10.f;
                voctPCV[p][v] = (10.f * random::uniform()) - 5.f;
				velocityPCV[p][v] = 10.f * random::uniform();
				dataOnePCV[p][v] = (10.f * random::uniform()) - 5.f;
				dataTwoPCV[p][v] = (10.f * random::uniform()) - 5.f;
				probabilityPCV[p][v] = 10.f * random::uniform();
		}
	}
	syncKnobs();
	shouldRefreshDisplay = true;
	invalidateStepRolls();
}

void SixtyFourGatePitchSeqKnobs::onReset(const ResetEvent& e){
	Module::onReset(e);
	Module::onReset(e);
	const int pD = 10;
    const int vD = 64;
	for (int p = 0; p < pD; p++) {
        for (int v = 0; v < vD; v++) {
				voctPCV[p][v] = 0.0f;
				velocityPCV[p][v] = 0.0f;
				dataOnePCV[p][v] = 0.0f;
				dataTwoPCV[p][v] = 0.0f;
				// Every step starts enabled, with a note slot ready and a chance of firing
				probabilityPCV[p][v] = 10.0f;
				gatePCV[p][v] = 10.0f;
				hasRecordedDataPCV[p][v] = 10.0f;
		}
	}
	for (int i = 0; i < 64; i++) {
		editModeSelectedGates[i] = false;
	}
	syncKnobs();
	shouldRefreshDisplay = true;
	invalidateStepRolls();
}


////////////////
//// WIDGET
////////////////

// One grid cell, and how the knob and its step button sit inside it
static const float CELL_MM = 14.77f;
static const float KNOB_OFFSET_MM = -2.0f;
static const float BUTTON_OFFSET_MM = 3.6f;
static const math::Vec KNOB_BOX_MM = math::Vec(9.4f, 9.4f);
static const math::Vec BUTTON_LIGHT_MM = math::Vec(2.7f, 2.7f);
static const math::Vec PLAYHEAD_MM = math::Vec(5.4f, 5.4f);

/** Wash a panel coloured disc over a step's cell parts while the step is inactive.
Both the knob and the step button use this so a cell reads as one state. */
static void drawStepShade(const widget::Widget::DrawArgs& args, math::Vec size, float shade) {
	if (shade <= 0.f)
		return;
	NVGcolor wash = nvgRGB(0x0b, 0x0b, 0x0b);
	wash.a = shade;
	math::Vec c = size.div(2.f);
	nvgBeginPath(args.vg);
	nvgCircle(args.vg, c.x, c.y, std::min(size.x, size.y) * 0.5f);
	nvgFillColor(args.vg, wash);
	nvgFill(args.vg);
}

/** The step knob: a static well under a rotating cap. */
struct StepKnob : app::SvgKnob {
	widget::SvgWidget* bg;
	int stepIndex = -1;
	StepKnob() {
		minAngle = -0.83f * M_PI;
		maxAngle = 0.83f * M_PI;
		bg = new widget::SvgWidget;
		bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/n_KnobBase.svg")));
		fb->addChildBelow(bg, tw);
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/n_KnobCap.svg")));
		box.size = mm2px(KNOB_BOX_MM);
		fb->box.size = box.size;
		tw->box.size = box.size;
		bg->box.size = box.size;
		shadow->box.size = box.size;
		shadow->box.pos = math::Vec(0, box.size.y * 0.10f);
		fb->setDirty();
	}
	/** Wash the knob out when its step is inactive, so it matches its unlit button. */
	void draw(const DrawArgs& args) override {
		app::SvgKnob::draw(args);
		if (stepIndex < 0)
			return;
		SixtyFourGatePitchSeqKnobs* m = dynamic_cast<SixtyFourGatePitchSeqKnobs*>(this->module);
		if (m)
			drawStepShade(args, box.size, m->knobShade[stepIndex]);
	}
};

/** Half size version of the KWA Control 8 rubber step button. Momentary, like the
step buttons on the KWA Pitch 64, so the module owns the toggling and the button can
be reused across the Record / Edit / Gates modes without latching into a state. */
struct StepButtonSwitch : app::SvgSwitch {
	StepButtonSwitch() {
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/n_StepButtonSmall.svg")));
	}
	// Drive the param straight from the press rather than through Switch's internal
	// momentary flags, so the module always sees a plain 1 while held and a 0 when not.
	void onButton(const ButtonEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) {
			app::SvgSwitch::onButton(e);
			return;
		}
		if (!getParamQuantity())
			return;
		getParamQuantity()->setValue(e.action == GLFW_PRESS ? 1.f : 0.f);
		e.consume(this);
		e.stopPropagating();
	}
	void onDragStart(const DragStartEvent& e) override {}
	// The press can end away from the button, so release on the drag end too
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && getParamQuantity())
			getParamQuantity()->setValue(0.f);
	}
};

/** Sizes of the two lights that sit on a step button, to match the smaller button. */
template <typename TBase>
struct StepButtonLight : TBase {
	StepButtonLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(BUTTON_LIGHT_MM);
	}
};

template <typename TBase>
struct StepPlayheadLight : TBase {
	StepPlayheadLight() {
		this->borderColor = color::BLACK_TRANSPARENT;
		this->bgColor = color::BLACK_TRANSPARENT;
		this->box.size = mm2px(PLAYHEAD_MM);
	}
};

/** Step button, washed out when its step is inactive so it matches the knob. */
template <typename TBase>
struct DimmableStepButton : TBase {
	int stepIndex = -1;
	void draw(const widget::Widget::DrawArgs& args) override {
		TBase::draw(args);
		if (stepIndex < 0)
			return;
		SixtyFourGatePitchSeqKnobs* m = dynamic_cast<SixtyFourGatePitchSeqKnobs*>(this->module);
		if (m)
			drawStepShade(args, this->box.size, m->buttonShade[stepIndex]);
	}
};

using StepButton = DimmableStepButton<LightButton<StepButtonSwitch, StepButtonLight<componentlibrary::RedGreenBlueLight>>>;
using StepPlayhead = StepPlayheadLight<componentlibrary::GreenLight>;

/** LightButton centres its light in its constructor, before addFrame has sized the
button, so it lands on the top left corner instead of the middle. Put it back. */
static StepButton* createStepButton(math::Vec pos, SixtyFourGatePitchSeqKnobs* module, int step) {
	StepButton* button = createLightParam<StepButton>(pos, module,
			SixtyFourGatePitchSeqKnobs::STEP_BUTTON_PARAMS + step,
			SixtyFourGatePitchSeqKnobs::STEP_LIGHTS + step * 3);
	button->stepIndex = step;
	button->getLight()->box.pos = button->box.size.div(2.f).minus(button->getLight()->box.size.div(2.f));
	return button;
}


SixtyFourGatePitchSeqKnobsWidget::SixtyFourGatePitchSeqKnobsWidget(SixtyFourGatePitchSeqKnobs* module) {
	setModule(module);
	setPanel(createPanel(asset::plugin(pluginInstance, "res/n_SixtyFourGatePitchSeqKnobs.svg")));

	// screws
	addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	//[0 - 63] - knob and step button per step, knob up left, button bottom right
	const float spacing = CELL_MM;
	const float xStartDistance = 22.39f;
	const float yStartDistance = 13.0f;
	for (int iy = 0; iy < 8; iy++) {
		for (int ix = 0; ix < 8; ix++) {
			const int iterator = iy * 8 + ix;
			const math::Vec cell = mm2px(math::Vec(xStartDistance + (ix * spacing), yStartDistance + (iy * spacing)));
			const math::Vec knobCentre = cell.plus(mm2px(math::Vec(KNOB_OFFSET_MM, KNOB_OFFSET_MM)));
			const math::Vec buttonCentre = cell.plus(mm2px(math::Vec(BUTTON_OFFSET_MM, BUTTON_OFFSET_MM)));

			// Playhead glow behind the button, added first so it sits under the button
			addChild(createLightCentered<StepPlayhead>(buttonCentre, module,
					SixtyFourGatePitchSeqKnobs::PLAYHEAD_LIGHTS + iterator));

			// The knob, positioned by its centre
			auto* knob = new StepKnob;
			knob->stepIndex = iterator;
			knob->box.pos = knobCentre.minus(mm2px(KNOB_BOX_MM).div(2.f));
			knob->module = module;
			knob->paramId = SixtyFourGatePitchSeqKnobs::STEP_KNOB_PARAMS + iterator;
			knob->initParamQuantity();
			addParam(knob);

			// The step button, positioned by its centre
			addParam(createStepButton(buttonCentre.minus(mm2px(math::Vec(2.2f, 2.2f))), module, iterator));
		}
	}

	float PortY[8] = {0};
	const float inputPortSpacing = 14.77f;
	const float inputPortStartHeight = 13.0f;
	for (int i = 0; i < 8; i++) {
		PortY[i] = inputPortStartHeight + inputPortSpacing * i;
	}
	const float inputPortXPos = 7.62f;
	// Inputs
	//[0] - Clock
	addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[0])), module, SixtyFourGatePitchSeqKnobs::CLOCK_INPUT));
	//[1] - Reset
	addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[1])), module, SixtyFourGatePitchSeqKnobs::RESET_INPUT));

	// Buttons
	//[0] - Record mode
	addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(inputPortXPos, PortY[2])), module, SixtyFourGatePitchSeqKnobs::RECORD_MODE_PARAM, SixtyFourGatePitchSeqKnobs::RECORD_MODE_LIGHT));
	//[1] - Edit mode
	addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(inputPortXPos, PortY[3])), module, SixtyFourGatePitchSeqKnobs::EDIT_MODE_PARAM, SixtyFourGatePitchSeqKnobs::EDIT_MODE_LIGHT));
	//[2] - Gate mode
	addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(inputPortXPos, PortY[4])), module, SixtyFourGatePitchSeqKnobs::GATE_MODE_PARAM, SixtyFourGatePitchSeqKnobs::GATE_MODE_LIGHT));

	//[5] - V/Oct
	addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[5])), module, SixtyFourGatePitchSeqKnobs::VOCT_INPUT));
	//[6] - Gate
	addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[6])), module, SixtyFourGatePitchSeqKnobs::GATE_INPUT));
	//[7] - Velocity
	addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[7])), module, SixtyFourGatePitchSeqKnobs::VELOCITY_INPUT));

	const float outputPortXPos = 139.7f;
	// outputs
	//[0] - Clock
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[0])), module, SixtyFourGatePitchSeqKnobs::CLOCK_OUTPUT));
	//[1] - Reset
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[1])), module, SixtyFourGatePitchSeqKnobs::RESET_OUTPUT));
	//[2] - Eoc
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[2])), module, SixtyFourGatePitchSeqKnobs::EOC_OUTPUT));

	// Knobs
	//[0] - Step count
	addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(outputPortXPos, PortY[3])), module, SixtyFourGatePitchSeqKnobs::STEP_COUNT_PARAM));
	//[1] - Display mode
	addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(outputPortXPos, PortY[4])), module, SixtyFourGatePitchSeqKnobs::DISPLAY_MODE_PARAM));

	//[3] - V/Oct
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[5])), module, SixtyFourGatePitchSeqKnobs::VOCT_OUTPUT));
	//[4] - Gate
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[6])), module, SixtyFourGatePitchSeqKnobs::GATE_OUTPUT));
	//[5] - Velocity
	addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[7])), module, SixtyFourGatePitchSeqKnobs::VELOCITY_OUTPUT));
}

void SixtyFourGatePitchSeqKnobsWidget::appendContextMenu(ui::Menu* menu) {
	SixtyFourGatePitchSeqKnobs* module = getModule<SixtyFourGatePitchSeqKnobs>();
	if (!module)
		return;
	menu->addChild(createIndexSubmenuItem("Knob Range",
		{"-10 to 10", "0 to 10"},
		[=]() { return module->params[SixtyFourGatePitchSeqKnobs::KNOB_RANGE_PARAM].getValue() > 0.5f ? 1 : 0; },
		[=](size_t index) { module->params[SixtyFourGatePitchSeqKnobs::KNOB_RANGE_PARAM].setValue(index == 1 ? 1.f : 0.f); }
	));
}


Model* modelSixtyFourGatePitchSeqKnobs = createModel<SixtyFourGatePitchSeqKnobs, SixtyFourGatePitchSeqKnobsWidget>("SixtyFourGatePitchSeqKnobs");
