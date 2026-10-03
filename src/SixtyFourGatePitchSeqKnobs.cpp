#include "SixtyFourGatePitchSeqKnobs.hpp"

// How much an inactive step's knob and button are washed out
static const float SHADE_KNOB_MUTED = 0.55f;        // inside the step count, trigger muted
static const float SHADE_KNOB_OUT_OF_RANGE = 0.93f; // past the step count
static const float SHADE_BUTTON_MUTED = 0.78f;      // darker, the button is much smaller
static const float SHADE_BUTTON_OUT_OF_RANGE = 0.93f;

////////////////
//// MODULE
////////////////

SixtyFourGatePitchSeqKnobs::SixtyFourGatePitchSeqKnobs() {
	//knobs
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	for (int i = 0; i < 64; i++) {
		configParam(STEP_KNOB_PARAMS + i, -10.f, 10.f, 0.f, string::f("Step %d V/Oct", i + 1), "V");
		//buttons - the middle of each knob
		configButton(STEP_BUTTON_PARAMS + i, string::f("Step %d", i + 1));
	}
	configButton(RECORD_MODE_PARAM, "Record");
	configButton(EDIT_MODE_PARAM, "Edit Mode");
	configSwitch(GATE_MODE_PARAM, 0.f, 10.f, 10.f, "Gate Mode");

	//knobs
	configSwitch(DISPLAY_MODE_PARAM, 1.f, 5.f, 2.f, "Display Mode", {"Gates", "V/Oct", "Velocity", "Data 1", "Data 2"});
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

float* SixtyFourGatePitchSeqKnobs::stepValuePtr(int i) {
	switch (getDataSource()) {
		case 3: return (float*)&velocityPCV[sequencePage][0][i];
		case 4: return (float*)&dataOnePCV[sequencePage][0][i];
		case 5: return (float*)&dataTwoPCV[sequencePage][0][i];
		default: return (float*)&voctPCV[sequencePage][0][i];
	}
}

void SixtyFourGatePitchSeqKnobs::syncKnob(int i) {
	if (i < 0 || i >= 64)
		return;
	ParamQuantity* quantity = getParamQuantity(STEP_KNOB_PARAMS + i);
	const float lo = getKnobMin();
	quantity->minValue = lo;
	quantity->maxValue = 10.f;
	quantity->setValue(math::clamp(*stepValuePtr(i), lo, 10.f));
	knobEcho[i] = quantity->getValue();
}

void SixtyFourGatePitchSeqKnobs::updateKnobLabels() {
	const int mode = getDataSource();
	const char* what = "V/Oct";
	if (mode == 3) what = "Velocity";
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
	const float lo = getKnobMin();
	for (int i = 0; i < 64; i++) {
		ParamQuantity* quantity = getParamQuantity(STEP_KNOB_PARAMS + i);
		const float knobValue = quantity->getValue();
		if (knobValue == knobEcho[i])
			continue;
		// The knob was turned, so write it back into the step it addresses
		knobEcho[i] = knobValue;
		*stepValuePtr(i) = math::clamp(knobValue, lo, 10.f);
		shouldRefreshDisplay = true;
	}
}

void SixtyFourGatePitchSeqKnobs::process(const ProcessArgs& args) {
	//channels
	int channels = std::min(std::max(inputs[GATE_INPUT].getChannels(), 1), 16);
	//less often process - for sample insensitive code
	if (processCounter >= 49) {
		processFifty(args, channels);
		processCounter = 0;
	}
	processCounter++;
	outputs[GATE_OUTPUT].setChannels(channels);
	outputs[VOCT_OUTPUT].setChannels(channels);
	outputs[VELOCITY_OUTPUT].setChannels(channels);

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
		leftSendMessage[0] = dataOnePCV[sequencePage][0][sentStep];
		leftSendMessage[1] = dataTwoPCV[sequencePage][0][sentStep];
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
		for (int c = 0; c < channels; c++) {
			if(hasRecordedDataPCV[sequencePage][c][currentStep] > 0 && !(inputs[GATE_INPUT].getVoltage(c) > 0)){
				outputs[VOCT_OUTPUT].setVoltage(voctPCV[sequencePage][c][currentStep], c);
				outputs[VELOCITY_OUTPUT].setVoltage(velocityPCV[sequencePage][c][currentStep], c);
			}
		}
	}

	// Gate Rise + Fall
	for (int c = 0; c < channels; c++) {
		engines[c].gateFellThisSample = false;
		engines[c].gateRoseThisSample = false;
		float gateVoltage = inputs[GATE_INPUT].getVoltage(c);
				// HIGH to LOW
			if (engines[c].isGateHigh) {
				if (gateVoltage <= 0.1f) {
					engines[c].isGateHigh = false;
					engines[c].gateFellThisSample = true;
				}
			}
			else {
				// LOW to HIGH
				if (gateVoltage >= 2.f) {
					engines[c].isGateHigh = true;
					engines[c].gateRoseThisSample = true;
				}
			}
	}

	// Voct Movement Detection
	for (int c = 0; c < channels; c++) {
		engines[c].voctMovedThisSample = false;
		float newVoctVoltage = inputs[VOCT_INPUT].getVoltage(c);
		if ((newVoctVoltage != engines[c].voctVoltage) && engines[c].isGateHigh){
			engines[c].voctMovedThisSample = true;
			engines[c].voctVoltage = newVoctVoltage;
		}
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
		for (int c = 0; c < channels; c++) {
			engines[c].hasTriggerOutputPulsedThisStep = false;
			if(currentStep < 64 && gatePCV[sequencePage][c][currentStep] > 0){
				engines[c].triggerOutputPulse.trigger(1e-3f);
				engines[c].hasTriggerOutputPulsedThisStep = true;
			}
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
			for (int c = 0; c < channels; c++) {
				engines[c].hasTriggerOutputPulsedThisStep = false;
			}
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
				}
				//Pulse outputs if gate
				if (currentStep < 64) {
					for (int c = 0; c < channels; c++) {
						if(gatePCV[sequencePage][c][currentStep] > 0){
							if(!engines[c].hasTriggerOutputPulsedThisStep){
								engines[c].triggerOutputPulse.trigger(1e-3f);
								engines[c].hasTriggerOutputPulsedThisStep = true;
							}
						}
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
			}
		}
		shouldRefreshDisplay = true;
	}

	for (int c = 0; c < channels; c++) {
		// On input gate rise
		if ((engines[c].gateRoseThisSample || engines[c].voctMovedThisSample) && currentStep < 64)
		{
			// recording
			if(isRecording)
			{
				if(editCount == 0) {
					clearNoteParams(currentWorkingStep, channels);
				}
				// Increment edit count, To reset note params only once per step
				editCount++;

				// Record Data
				gatePCV[sequencePage][c][currentWorkingStep] = 10.f;
				hasRecordedDataPCV[sequencePage][c][currentWorkingStep] = 10.f;
				voctPCV[sequencePage][c][currentWorkingStep] = inputs[VOCT_INPUT].getVoltage(c);
				velocityPCV[sequencePage][c][currentWorkingStep] = inputs[VELOCITY_INPUT].getVoltage(c);

				// The matching knob follows what was just recorded
				syncKnob(currentWorkingStep);

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
									clearNoteParams(j, channels);
								}
							}
						}
						// Edit Data
						gatePCV[sequencePage][c][i] = 10.f;
						hasRecordedDataPCV[sequencePage][c][i] = 10.f;
						voctPCV[sequencePage][c][i] = inputs[VOCT_INPUT].getVoltage(c);
						velocityPCV[sequencePage][c][i] = inputs[VELOCITY_INPUT].getVoltage(c);
						// Increment edit count, To reset note params only once per edit
						editCount++;
						syncKnob(i);
					}
				}
				// Start counting up clock steps again
				clockStepsSinceLastEdit = 0;
			}
		}
	}

	for (int c = 0; c < channels; c++) {
		if(engines[c].gateFellThisSample){
			outputs[GATE_OUTPUT].setVoltage(0.f, c);
		}
	}

	// Output - pulses
	for (int c = 0; c < channels; c++) {
		outputs[GATE_OUTPUT].setVoltage((engines[c].triggerOutputPulse.process(args.sampleTime) ? 10.f : 0.f) > inputs[GATE_INPUT].getVoltage(c) ? 10.f : inputs[GATE_INPUT].getVoltage(c),c);
	}
	outputs[RESET_OUTPUT].setVoltage(inputs[RESET_INPUT].getVoltage());
	outputs[EOC_OUTPUT].setVoltage(eocOutputPulse.process(args.sampleTime) ? 10.f : 0.f);
	outputs[CLOCK_OUTPUT].setVoltage(clockOutputPulse.process(args.sampleTime) ? 10.f : 0.f);

}

void SixtyFourGatePitchSeqKnobs::processFifty(const ProcessArgs& args, int channels){
	if(!wasInitialized){
		params[GATE_MODE_PARAM].setValue(10.f);
		wasInitialized = true;
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
			copyNotes(channels, false);
		}
	} else {
		copyPressedDown = false;
	}
	if(expanderSignalCut > 0.1f){
		if(!cutPressedDown){
			//DEBUG("Cut pressed down");
			cutPressedDown = true;
			cutNotes(channels);
		}
	} else {
		cutPressedDown = false;
	}
	if(expanderSignalPaste > 0.1f){
		if(!pastePressedDown){
			//DEBUG("Paste pressed down");
			pastePressedDown = true;
			pasteNotes(channels);
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
					bool gateOn = false;
					for (int c = 0; c < 16; c++) {
						if (gatePCV[sequencePage][c][i] > 0.1f) {
							gateOn = true;
						}
					}
					if (gateOn) {
						for (int c = 0; c < 16; c++) {
							gatePCV[sequencePage][c][i] = 0.f;
						}
					} else {
						gatePCV[sequencePage][0][i] = 10.f;
						hasRecordedDataPCV[sequencePage][0][i] = 10.f;
					}
				}
				// Edit mode - select or deselect this step only
				else {
					if (!anyPressed) {
						editModeSelectedGates[i] = !editModeSelectedGates[i];
						anyPressed = true;
					}
				}
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
		updateDisplay(channels);
	}

	//Audition
	for (int c = 0; c < channels; c++) {
		if(inputs[GATE_INPUT].getVoltage(c) > 0.f){
			outputs[GATE_OUTPUT].setVoltage(inputs[GATE_INPUT].getVoltage(c),c);
			outputs[VOCT_OUTPUT].setVoltage(inputs[VOCT_INPUT].getVoltage(c),c);
			outputs[VELOCITY_OUTPUT].setVoltage(inputs[VELOCITY_INPUT].getVoltage(c),c);
		}
	}
}

void SixtyFourGatePitchSeqKnobs::copyNotes(int channels, bool cut) {
	for (int c = 0; c < channels; c++) {
		for(int i = 0; i < 64; i++){
			if(editModeSelectedGates[i]){
				gateCopy[c] = gatePCV[sequencePage][c][i] > 0.1f;
				voctCopy[c] = voctPCV[sequencePage][c][i];
				velocityCopy[c] = velocityPCV[sequencePage][c][i];
				hasRecordedDataCopy[c] = hasRecordedDataPCV[sequencePage][c][i] > 0.1f;
				dataOneCopy[c] = dataOnePCV[sequencePage][c][i];
				dataTwoCopy[c] = dataTwoPCV[sequencePage][c][i];
				selectionCopy[c] = true;
				//todo multi select, require first selected index, paste transforms and loops at max step count
			}
		}
	}
	for (int i = 0; i < 64; i++) {
		if(cut){
			if(editModeSelectedGates[i]){
				clearNoteParams(i, 16);
			}
		}
		editModeSelectedGates[i] = false;
	}
	shouldRefreshDisplay = true;
}

void SixtyFourGatePitchSeqKnobs::cutNotes(int channels) {
	copyNotes(channels, true);
}

void SixtyFourGatePitchSeqKnobs::pasteNotes(int channels) {
	for (int c = 0; c < channels; c++) {
		for(int i = 0; i < 64; i++){
			if(editModeSelectedGates[i]){
				gatePCV[sequencePage][c][i] = gateCopy[c] ? 10.f : 0.f;
				voctPCV[sequencePage][c][i] = voctCopy[c];
				velocityPCV[sequencePage][c][i] = velocityCopy[c];
				hasRecordedDataPCV[sequencePage][c][i] = hasRecordedDataCopy[c] ? 10.f : 0.f;
				dataOnePCV[sequencePage][c][i] = dataOneCopy[c];
				dataTwoPCV[sequencePage][c][i] = dataTwoCopy[c];
			}
		}
	}
	for (int i = 0; i < 64; i++) {
		editModeSelectedGates[i] = false;
	}
	syncKnobs();
	shouldRefreshDisplay = true;
}

void SixtyFourGatePitchSeqKnobs::setModeBrightnesses() {
	// Set Mode Brightnesses
	lights[RECORD_MODE_LIGHT].setBrightness(isRecording);
	lights[EDIT_MODE_LIGHT].setBrightness(!gateModeSelected);
	lights[GATE_MODE_LIGHT].setBrightness(gateModeSelected);
}

void SixtyFourGatePitchSeqKnobs::clearNoteParams(int i, int channels) {
	for (int c = 0; c < channels; c++){
		gatePCV[sequencePage][c][i] = 0.f;
		velocityPCV[sequencePage][c][i] = 0.f;
		voctPCV[sequencePage][c][i] = 0.f;
		hasRecordedDataPCV[sequencePage][c][i] = 0.f;
		dataOnePCV[sequencePage][c][i] = 0.f;
		dataTwoPCV[sequencePage][c][i] = 0.f;
	}
}

void SixtyFourGatePitchSeqKnobs::updateDisplay(int channels){
	float red = 0;
	float green = 0;
	float blue = 0;
	float prevMinVoct = minVoct;
	float prevMaxVoct = maxVoct;
	minVoct = 10.f;
	maxVoct = -10.f;
	float prevMinVel = minVel;
	float prevMaxVel = maxVel;
	minVel = 10.f;
	maxVel = -10.f;
	float prevMinDataOne = minDataOne;
	float prevMaxDataOne = maxDataOne;
	minDataOne = 10.f;
	maxDataOne = -10.f;
	float prevMinDataTwo = minDataTwo;
	float prevMaxDataTwo = maxDataTwo;
	minDataTwo = 10.f;
	maxDataTwo = -10.f;

	const int displayMode = getDataSource();

	for (int i = 0; i < 64; i++) {
		//min max
		float voct = 0.f;
		float vel = 0.f;
		float gateVal = -10.f;
		float dataOne = 0.f;
		float dataTwo = 0.f;
		float voctSum = 0.f;
		float velSum = 0.f;
		float dataOneSum = 0.f;
		float dataTwoSum = 0.f;
		int writtenChannelCount = 0;
		for (int c = 0; c < channels; c++) {
			if(gatePCV[sequencePage][c][i] > 0.1f){
				voctSum = voctSum + voctPCV[sequencePage][c][i];
				velSum = velSum + velocityPCV[sequencePage][c][i];
				dataOneSum = dataOneSum + dataOnePCV[sequencePage][c][i];
				dataTwoSum = dataTwoSum + dataTwoPCV[sequencePage][c][i];
				gateVal = 10.f;
				writtenChannelCount++;
			}
		}
		// Guard the divide, a step without a gate has nothing to average
		if (writtenChannelCount > 0) {
			voct = (voctSum / writtenChannelCount);
			vel = (velSum / writtenChannelCount);
			dataOne = (dataOneSum / writtenChannelCount);
			dataTwo = (dataTwoSum / writtenChannelCount);
		}

		bool written = false;
		for (int c = 0; c < channels; c++) {
			if (hasRecordedDataPCV[sequencePage][c][i] > 0.1f){
				written = true;
			}
		}
		//write min maxes
		if(written && i < (stepCountInt)){
			minVoct = voct < minVoct ? voct : minVoct;
			maxVoct = voct > maxVoct ? voct : maxVoct;
			minVel = vel < minVel ? vel : minVel;
			maxVel = vel > maxVel ? vel : maxVel;
			minDataOne = dataOne < minDataOne ? dataOne : minDataOne;
			maxDataOne = dataOne > maxDataOne ? dataOne : maxDataOne;
			minDataTwo = dataTwo < minDataTwo ? dataTwo : minDataTwo;
			maxDataTwo = dataTwo > maxDataTwo ? dataTwo : maxDataTwo;
		}
		//Display Mode Brightness
		if(i <= (stepCountInt - 1)) {
			switch(displayMode){
				//[1] - Gate display
				case 1:
					blue = written ? 1.f : 0.f;
					//if iterating through the current step, divide the gate value by 10 (1.f fully lit) else divide by 12 = .83f (83% brightness)
					if (!written) {
						blue = gateVal / (i == currentStep ? 10.f : 12.f);
					}
					break;

				//[2] - Voct display
				case 2:
					//normalize colors unless min and max are the same, then it should just be green :3
					red = (prevMaxVoct == prevMinVoct) ? 0.f : 1 - ((voct - prevMinVoct) / (prevMaxVoct - prevMinVoct));
					green = (prevMaxVoct == prevMinVoct) ? 1.f : (voct - prevMinVoct) / (prevMaxVoct - prevMinVoct);
					//blue should be blank unless no voct then we do dull blue (10%) to show that theres still a gate there
					if (!written) {
						blue = gateVal / 100.f;
					}
					break;

				//[3] - Velocity display
				case 3:
					//normalize velocity unless min and max are the same, then it should just be bright green :3
					red = (prevMaxVel == prevMinVel) ? 1.f : ((vel - prevMinVel) / (prevMaxVel - prevMinVel));
					//minimum 0.5 on vel
					red = red < .05f ? .05f : red;
					if (!written) {
						blue = gateVal / 100.f;
					}
					break;

				//[4] - Data one display
				case 4:
					green = (prevMaxDataOne == prevMinDataOne) ? 1.f : ((dataOne - prevMinDataOne) / (prevMaxDataOne - prevMinDataOne));
					if (written) {
						blue = green;
					} else {
						green = 0.f;
						blue = gateVal / 100.f;
					}
					break;

				//[5] - Data two display
				case 5:
				default:
					red = (prevMaxDataTwo == prevMinDataTwo) ? 1.f : ((dataTwo - prevMinDataTwo) / (prevMaxDataTwo - prevMinDataTwo));
					if (written) {
						blue = red;
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

		//Active step - a bright ring around the knob that is playing
		lights[PLAYHEAD_LIGHTS + i].setBrightness(i == currentStep ? 1.f : 0.f);
	}
	//reset vars so we dont iterate every sample
	wasButtonPressedThisSample = false;
	shouldRefreshDisplay = false;
}

json_t* SixtyFourGatePitchSeqKnobs::dataToJson() {
	// PCV Dimensions
    const int pD = 10;
    const int cD = 16;
    const int vD = 64;

	json_t* rootJ = json_object(); // Create a JSON object

	json_t* gate3d = json_array();
	json_t* voct3d = json_array();
	json_t* rec3d = json_array();
	json_t* vel3d = json_array();
	json_t* dataOne3d = json_array();
	json_t* dataTwo3d = json_array();
	for (int p = 0; p < pD; p++) {
		json_t* gate2d = json_array();
		json_t* voct2d = json_array();
		json_t* rec2d = json_array();
		json_t* vel2d = json_array();
		json_t* dataOne2d = json_array();
		json_t* dataTwo2d = json_array();

		for (int c = 0; c < cD; c++) {
			json_t* gate1d = json_array();
			json_t* voct1d = json_array();
			json_t* rec1d = json_array();
			json_t* vel1d = json_array();
			json_t* dataOne1d = json_array();
			json_t* dataTwo1d = json_array();

			for (int v = 0; v < vD; v++) {
				json_array_append_new(gate1d, json_real(gatePCV[p][c][v]));
				json_array_append_new(voct1d, json_real(voctPCV[p][c][v]));
				json_array_append_new(rec1d, json_real(hasRecordedDataPCV[p][c][v]));
				json_array_append_new(vel1d, json_real(velocityPCV[p][c][v]));
				json_array_append_new(dataOne1d, json_real(dataOnePCV[p][c][v]));
				json_array_append_new(dataTwo1d, json_real(dataTwoPCV[p][c][v]));
			}
			json_array_append_new(gate2d, gate1d);
			json_array_append_new(voct2d, voct1d);
			json_array_append_new(rec2d, rec1d);
			json_array_append_new(vel2d, vel1d);
			json_array_append_new(dataOne2d, dataOne1d);
			json_array_append_new(dataTwo2d, dataTwo1d);
		}
		json_array_append_new(gate3d, gate2d);
		json_array_append_new(voct3d, voct2d);
		json_array_append_new(rec3d, rec2d);
		json_array_append_new(vel3d, vel2d);
		json_array_append_new(dataOne3d, dataOne2d);
		json_array_append_new(dataTwo3d, dataTwo2d);
	}
	json_object_set_new(rootJ, "gatePCV", gate3d);
	json_object_set_new(rootJ, "voctPCV", voct3d);
	json_object_set_new(rootJ, "hasRecordedDataPCV", rec3d);
	json_object_set_new(rootJ, "velocityPCV", vel3d);
	json_object_set_new(rootJ, "dataOnePCV", dataOne3d);
	json_object_set_new(rootJ, "dataTwoPCV", dataTwo3d);

	return rootJ;
}

void SixtyFourGatePitchSeqKnobs::dataFromJson(json_t* rootJ) {
	// PCV Dimensions
    const int pD = 10;
    const int cD = 16;
    const int vD = 64;

	json_t* gate3D = json_object_get(rootJ, "gatePCV");
	json_t* voct3D = json_object_get(rootJ, "voctPCV");
	json_t* rec3D = json_object_get(rootJ, "hasRecordedDataPCV");
	json_t* vel3D = json_object_get(rootJ, "velocityPCV");
	json_t* dataOne3D = json_object_get(rootJ, "dataOnePCV");
	json_t* dataTwo3D = json_object_get(rootJ, "dataTwoPCV");
    if (gate3D && json_is_array(gate3D) &&
		voct3D && json_is_array(voct3D) &&
		rec3D && json_is_array(rec3D) &&
		vel3D && json_is_array(vel3D) &&
		dataOne3D && json_is_array(dataOne3D) &&
		dataTwo3D && json_is_array(dataTwo3D)) {
        for (int p = 0; p < pD; p++) {
            json_t* gate2D = json_array_get(gate3D, p);
			json_t* voct2D = json_array_get(voct3D, p);
			json_t* rec2D = json_array_get(rec3D, p);
			json_t* vel2D = json_array_get(vel3D, p);
			json_t* dataOne2D = json_array_get(dataOne3D, p);
			json_t* dataTwo2D = json_array_get(dataTwo3D, p);
            if (gate2D && json_is_array(gate2D) &&
				voct2D && json_is_array(voct2D) &&
				rec2D && json_is_array(rec2D) &&
				vel2D && json_is_array(vel2D) &&
				dataOne2D && json_is_array(dataOne2D) &&
				dataTwo2D && json_is_array(dataTwo2D)) {
                for (int c = 0; c < cD; c++) {
                    json_t* gate1D = json_array_get(gate2D, c);
					json_t* voct1D = json_array_get(voct2D, c);
					json_t* rec1D = json_array_get(rec2D, c);
					json_t* vel1D = json_array_get(vel2D, c);
					json_t* dataOne1D = json_array_get(dataOne2D, c);
					json_t* dataTwo1D = json_array_get(dataTwo2D, c);
                    if (gate1D && json_is_array(gate1D) &&
						voct1D && json_is_array(voct1D) &&
						rec1D && json_is_array(rec1D) &&
						vel1D && json_is_array(vel1D) &&
						dataOne1D && json_is_array(dataOne1D) &&
						dataTwo1D && json_is_array(dataTwo1D)) {
                        for (int v = 0; v < vD; v++) {
                            json_t* gateValue = json_array_get(gate1D, v);
                            if (gateValue && json_is_real(gateValue)) {
                                gatePCV[p][c][v] = json_real_value(gateValue);
                            }
							json_t* voctValue = json_array_get(voct1D, v);
                            if (voctValue && json_is_real(voctValue)) {
                                voctPCV[p][c][v] = json_real_value(voctValue);
                            }
							json_t* recValue = json_array_get(rec1D, v);
                            if (recValue && json_is_real(recValue)) {
                                hasRecordedDataPCV[p][c][v] = json_real_value(recValue);
                            }
							json_t* velValue = json_array_get(vel1D, v);
                            if (velValue && json_is_real(velValue)) {
                                velocityPCV[p][c][v] = json_real_value(velValue);
                            }
							json_t* dataOneValue = json_array_get(dataOne1D, v);
                            if (dataOneValue && json_is_real(dataOneValue)) {
                                dataOnePCV[p][c][v] = json_real_value(dataOneValue);
                            }
							json_t* dataTwoValue = json_array_get(dataTwo1D, v);
                            if (dataTwoValue && json_is_real(dataTwoValue)) {
                                dataTwoPCV[p][c][v] = json_real_value(dataTwoValue);
                            }
                        }
                    }
                }
            }
        }
	}
	// The knobs mirror the stored values, so pull them back in
	syncKnobs();
	shouldRefreshDisplay = true;
}

void SixtyFourGatePitchSeqKnobs::onRandomize(const RandomizeEvent& e){
	Module::onRandomize(e);
	const int pD = 10;
    const int cD = 16;
    const int vD = 64;
	for (int p = 0; p < pD; p++) {
        for (int c = 0; c < cD; c++) {
            for (int v = 0; v < vD; v++) {
				hasRecordedDataPCV[p][c][v] = 10.f;
				gatePCV[p][c][v] = 10.f;
                voctPCV[p][c][v] = (10.f * random::uniform()) - 5.f;
				velocityPCV[p][c][v] = 10.f * random::uniform();
				dataOnePCV[p][c][v] = (10.f * random::uniform()) - 5.f;
				dataTwoPCV[p][c][v] = (10.f * random::uniform()) - 5.f;
			}
		}
	}
	syncKnobs();
	shouldRefreshDisplay = true;
}

void SixtyFourGatePitchSeqKnobs::onReset(const ResetEvent& e){
	Module::onReset(e);
	const int pD = 10;
    const int cD = 16;
    const int vD = 64;
	for (int p = 0; p < pD; p++) {
        for (int c = 0; c < cD; c++) {
            for (int v = 0; v < vD; v++) {
				hasRecordedDataPCV[p][c][v] = 0.0f;
				gatePCV[p][c][v] = 0.0f;
                voctPCV[p][c][v] = 0.0f;
				velocityPCV[p][c][v] = 0.0f;
				dataOnePCV[p][c][v] = 0.0f;
				dataTwoPCV[p][c][v] = 0.0f;
			}
		}
	}
	for (int i = 0; i < 64; i++) {
		editModeSelectedGates[i] = false;
	}
	syncKnobs();
	shouldRefreshDisplay = true;
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
