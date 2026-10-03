#include "SixtyFourGatePitchSeq.hpp"

SixtyFourGatePitchSeq::SixtyFourGatePitchSeq() {
	//buttons
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	for (int i = 0; i < 64; i++) {
		configButton(BUTTON_MATRIX_PARAMS + i, string::f("%d", i + 1));
	}
	configButton(RECORD_MODE_PARAM, "Record");
	configButton(EDIT_MODE_PARAM, "Edit Mode");
	configSwitch(GATE_MODE_PARAM, 0.f, 10.f, 10.f,"Gate Mode");

	//knobs
	configSwitch(DISPLAY_MODE_PARAM, 1.f, 5.f, 2.f, "Display Mode",{"Gates", "V/Oct", "Velocity", "Data 1", "Data 2"});
	getParamQuantity(DISPLAY_MODE_PARAM)->snapEnabled = true;
	configParam(STEP_COUNT_PARAM, 1.f, 64.f, 8.f, "Steps");
	getParamQuantity(STEP_COUNT_PARAM)->snapEnabled = true;

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

}

void SixtyFourGatePitchSeq::process(const ProcessArgs& args) {
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
		leftSendMessage[0] = 0.f;
		leftSendMessage[1] = 0.f;
		// Flip messages at the end of the timestep
		leftExpander.module->rightExpander.messageFlipRequested = true;
	}
	sequencePage = std::roundf(is_baby ? (expanderSignalPage - 1) : 0);
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
			if(hasRecordedDataPCV[sequencePage][c][currentStep] > 0.f && !(inputs[GATE_INPUT].getVoltage(c) > 0)){
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
		float newVoctVoltage = inputs[VOCT_INPUT].getVoltage();
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
				// dataOnePCV[sequencePage][c][currentWorkingStep] = ;
				// dataTwoPCV[sequencePage][c][currentWorkingStep] = 10.f;

			// in edit mode
			} else if (!gateModeSelected)
			{ 
				// Iterate buttons
				for (int i = 0; i < 64; i++) {
					// If button is selected with edit mode
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

void SixtyFourGatePitchSeq::processFifty(const ProcessArgs& args, int channels){
	if(!wasInitialized){
		params[GATE_MODE_PARAM].setValue(10.f);
		wasInitialized = true;
	}
	// Buttons
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

	// Iterate main button matrix to check for pressed buttons
	bool anyThisSet = false;
	for (int i = 0; i < 64; i++) {
		if(params[BUTTON_MATRIX_PARAMS + i].getValue() > 0.f){
			anyThisSet = true;
			wasButtonPressedThisSample = true;
			if(!gateModifiedSinceRelease){
				// Gate Mode
				if(gateModeSelected){
					bool gateFoundOnAnyChannel = false;
					for (int c = 0; c < channels; c++) {
						if(gatePCV[sequencePage][c][i] > 0.1f){
							gateFoundOnAnyChannel = true;
						}
					}
					clearNoteParams(i, channels);
					if(!gateFoundOnAnyChannel){
						gatePCV[sequencePage][0][i] = 10.f;
					}
				}
				gateModifiedSinceRelease = true;
			}
			
			// Edit Mode
			else{
				if(!anyPressed){
					editModeSelectedGates[i] = !editModeSelectedGates[i];
					anyPressed = true;
				}
			}
		}
	}
	if(!anyThisSet){
		anyPressed = false;
		gateModifiedSinceRelease = false;
	}


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

void SixtyFourGatePitchSeq::copyNotes(int channels, bool cut) {
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
				clearNoteParams(i, channels);
			}
		}
		editModeSelectedGates[i] = false;
	}
}

void SixtyFourGatePitchSeq::cutNotes(int channels) {
	copyNotes(channels, true);
}

void SixtyFourGatePitchSeq::pasteNotes(int channels) {
	for (int c = 0; c < channels; c++) {
		for(int i = 0; i < 64; i++){
			if(editModeSelectedGates[i]){
				gatePCV[sequencePage][c][i] = gateCopy[c] ? 10.f : 0.f;
				voctPCV[sequencePage][c][i] = voctCopy[c];
				velocityPCV[sequencePage][c][i] = velocityCopy[c];
				hasRecordedDataPCV[sequencePage][c][i] = hasRecordedDataCopy[c] ? 10.f : 0.f;
				dataOnePCV[sequencePage][c][i] = dataOneCopy[c];
				dataOnePCV[sequencePage][c][i] = dataTwoCopy[c];
			}
		}
	}
	for (int i = 0; i < 64; i++) {
		editModeSelectedGates[i] = false;
	}
}


void SixtyFourGatePitchSeq::setModeBrightnesses() {
	// Set Mode Brightnesses
	lights[RECORD_MODE_LIGHT].setBrightness(isRecording);
	lights[EDIT_MODE_LIGHT].setBrightness(!gateModeSelected);
	lights[GATE_MODE_LIGHT].setBrightness(gateModeSelected);
}

void SixtyFourGatePitchSeq::clearNoteParams(int i, int channels) {
	for (int c = 0; c < channels; c++){
		gatePCV[sequencePage][c][i] = 0.f;
		velocityPCV[sequencePage][c][i] = 0.f;
		voctPCV[sequencePage][c][i] = 0.f;
		hasRecordedDataPCV[sequencePage][c][i] = 0.f;
		dataOnePCV[sequencePage][c][i] = 0.f;
		dataTwoPCV[sequencePage][c][i] = 0.f;
	}
}

void SixtyFourGatePitchSeq::updateDisplay(int channels){
	float red = 0;
	float green = 0;
	float prevMinVoct = minVoct;
	float prevMaxVoct = maxVoct;
	minVoct = 10.f;
	maxVoct = -10.f;
	float prevMinVel = minVel;
	float prevMaxVel = maxVel;
	minVel = 10.f;
	maxVel = -10.f;

	for (int i = 0; i < 64; i++) {
		//min max
		float voct = -10.f;
		float vel = -10.f;
		float gateVal = -10.f;
		float voctSum = 0.f;
		float velSum = 0.f;
		int writtenChannelCount = 0.f;
		for (int c = 0; c < channels; c++) {
			if(gatePCV[sequencePage][c][i] > 0.1f){
				voctSum = voctSum + voctPCV[sequencePage][c][i];
				velSum = velSum + velocityPCV[sequencePage][c][i];
				gateVal = 10.f;
				writtenChannelCount++;
			}
		}
		voct = (voctSum / writtenChannelCount);
		vel = (velSum / writtenChannelCount);

		const int displayMode = params[DISPLAY_MODE_PARAM].getValue();
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
		}
		//Display Mode Brightness
		if(i <= (stepCountInt - 1)) {
			switch(displayMode){
				//[1] - Gate display
				case 1:
					lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(0.f);
					lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(0.f);
					//if iterating through the current step, divide the gate value by 10 (1.f fully lit) else divide by 12 = .83f (83% brightness)
					lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(gateVal / (i == currentStep ? 10.f : 12.f));
					break;

				//[2] - Voct display
				case 2:
					//normalize colors unless min and max are the same, then it should just be green :3
					red = (prevMaxVoct == prevMinVoct) ? 0.f : 1 - ((voct - prevMinVoct) / (prevMaxVoct - prevMinVoct));
					green = (prevMaxVoct == prevMinVoct) ? 1.f : (voct - prevMinVoct) / (prevMaxVoct - prevMinVoct);
					
					lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(written ? red : 0);
					lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(written ? green: 0);
					//blue should be blank unless no voct then we do dull blue (10%) to show that theres still a gate there
					lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(written ? 0 : gateVal / 100.f);
					break;

				//[3] - Velocity display
				case 3:
					//normalize velocity unless min and max are the same, then it should just be bright green :3
					red = (prevMaxVel == prevMinVel) ? 1.f : ((vel - prevMinVel) / (prevMaxVel - prevMinVel));
					//minimum 0.5 on vel
					red = red < .05f ? .05f : red;
					lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(written ? red : 0);
					lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(0.f);
					//blue should be blank unless no voct then we do dull blue (10%) to show that theres still a gate there
					lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(written ? 0 : gateVal / 100.f);

					break;
				default:
					break;
			}
		}else{
			//if over step count, dont display
			lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(0.f);
			lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(0.f);
			lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(0.f);
		}
		
		if(!gateModeSelected){
			//if we're in edit mode, light up selected gates
			if(editModeSelectedGates[i]){
				lights[STEP_LIGHTS + (i * 3) + 0].setBrightness(10.f);
				lights[STEP_LIGHTS + (i * 3) + 1].setBrightness(10.f);
				lights[STEP_LIGHTS + (i * 3) + 2].setBrightness(10.f);
			}
		}

		//Active step light
		lights[ACTIVE_STEP_LIGHTS + i].setBrightness(i == currentStep ? 0.8f : 0.f);
	}
	//reset vars so we dont iterate every sample
	wasButtonPressedThisSample = false;
	shouldRefreshDisplay = false;
}

json_t* SixtyFourGatePitchSeq::dataToJson() {
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

void SixtyFourGatePitchSeq::dataFromJson(json_t* rootJ) {
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
}

void SixtyFourGatePitchSeq::onRandomize(const RandomizeEvent& e){
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
}

void SixtyFourGatePitchSeq::onReset(const ResetEvent& e){
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
}

SixtyFourGatePitchSeqWidget::SixtyFourGatePitchSeqWidget(SixtyFourGatePitchSeq* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/n_SixtyFourGatePitchSeq.svg")));

		// screws
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//[0 - 63] - Matrix buttons / lights
		const float spacing = 14.77f;
		const float xStartDistance = 22.39f;
		const float yStartDistance = 13.0f;
		//Iterate Column
		for (int iy = 0; iy < 8; iy++) {
			//Iterate Row
			for (int ix = 0; ix < 8; ix++) {
				int iterator = iy * 8 + ix;
				addChild(createLightCentered<LargerButtonLight<RoundRectLight<GreenLight>>>(mm2px(Vec(xStartDistance + (ix * spacing), yStartDistance + (iy * spacing))), module, SixtyFourGatePitchSeq::ACTIVE_STEP_LIGHTS + iterator));
				addParam(createLightParamCentered<LightButton<RubberRectButtonLarge, LargerButtonCenterLight<RoundRectLight<RedGreenBlueLight>>>>(mm2px(Vec(xStartDistance + (ix * spacing), yStartDistance + (iy * spacing))), module, SixtyFourGatePitchSeq::BUTTON_MATRIX_PARAMS + iterator, SixtyFourGatePitchSeq::STEP_LIGHTS + iterator * 3));
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
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[0])), module, SixtyFourGatePitchSeq::CLOCK_INPUT));
		//[1] - Reset
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[1])), module, SixtyFourGatePitchSeq::RESET_INPUT));
		//[2] - V/Oct
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[5])), module, SixtyFourGatePitchSeq::VOCT_INPUT));
		//[3] - Gate
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[6])), module, SixtyFourGatePitchSeq::GATE_INPUT));
		//[4] - Velocity
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(inputPortXPos, PortY[7])), module, SixtyFourGatePitchSeq::VELOCITY_INPUT));

		// Buttons
		//[0] - Record mode
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<RedLight>>>(mm2px(Vec(inputPortXPos, PortY[2])), module, SixtyFourGatePitchSeq::RECORD_MODE_PARAM, SixtyFourGatePitchSeq::RECORD_MODE_LIGHT));
		//[1] - Edit mode
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(inputPortXPos, PortY[3])), module, SixtyFourGatePitchSeq::EDIT_MODE_PARAM, SixtyFourGatePitchSeq::EDIT_MODE_LIGHT));
		//[2] - Gate mode
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(inputPortXPos, PortY[4])), module, SixtyFourGatePitchSeq::GATE_MODE_PARAM, SixtyFourGatePitchSeq::GATE_MODE_LIGHT));

		const float outputPortXPos = 139.7f;
		// outputs
		//[0] - Clock
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[0])), module, SixtyFourGatePitchSeq::CLOCK_OUTPUT));
		//[1] - Reset
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[1])), module, SixtyFourGatePitchSeq::RESET_OUTPUT));
		//[2] - Eoc
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[2])), module, SixtyFourGatePitchSeq::EOC_OUTPUT));
		//[3] - V/Oct
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[5])), module, SixtyFourGatePitchSeq::VOCT_OUTPUT));
		//[4] - Gate
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[6])), module, SixtyFourGatePitchSeq::GATE_OUTPUT));
		//[5] - Velocity
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(outputPortXPos, PortY[7])), module, SixtyFourGatePitchSeq::VELOCITY_OUTPUT));

		// Knobs
		//[0] - Step count
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(outputPortXPos, PortY[3])), module, SixtyFourGatePitchSeq::STEP_COUNT_PARAM));
		//[1] - Display mode
addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(outputPortXPos, PortY[4])), module, SixtyFourGatePitchSeq::DISPLAY_MODE_PARAM));
	}


Model* modelSixtyFourGatePitchSeq = createModel<SixtyFourGatePitchSeq, SixtyFourGatePitchSeqWidget>("SixtyFourGatePitchSeq");
