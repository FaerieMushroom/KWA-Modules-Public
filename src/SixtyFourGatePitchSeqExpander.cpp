#include "KWAModules.hpp"


struct SixtyFourGatePitchSeqExpander : Module {
	enum ParamIds {
		PLAY_PARAM,
		ONESHOT_PARAM,
		PLAYHEAD_KNOB_PARAM,
		PAGE_KNOB_PARAM,
		COPY_BUTTON_PARAM,
		CUT_BUTTON_PARAM,
		PASTE_BUTTON_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PLAY_CV_INPUT,
		PLAYHEAD_CV_INPUT,
		PAGE_CV_INPUT,
		DATA_ONE_INPUT,
		DATA_TWO_INPUT,
		STEPS_CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		DATA_ONE_OUTPUT,
		DATA_TWO_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		PLAY_LIGHT,
		ONESHOT_LIGHT,
		COPY_LIGHT,
		CUT_LIGHT,
		PASTE_LIGHT,
		NUM_LIGHTS
	};
	// Expander Message Arrays
	float leftMessages[2][10] = {};
	float rightMessages[2][10] = {};
	float expanderSignalDataOne = 0.f;
	float expanderSignalDataTwo = 0.f;

	SixtyFourGatePitchSeqExpander() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//Buttons
		configButton(PLAY_PARAM, "Run");
		configButton(ONESHOT_PARAM, "One Shot");
		configButton(COPY_BUTTON_PARAM, "Copy");
		configButton(CUT_BUTTON_PARAM, "Cut");
		configButton(PASTE_BUTTON_PARAM, "Paste");

		//knobs
		configSwitch(PLAYHEAD_KNOB_PARAM, 1.f, 3.f, 1.f, "Step Mode",{"Ascend", "Descend", "Ping Pong"});
		getParamQuantity(PLAYHEAD_KNOB_PARAM)->snapEnabled = true;
		configParam(PAGE_KNOB_PARAM, 1.f, 10.f, 1.f, "Page");
		getParamQuantity(PAGE_KNOB_PARAM)->snapEnabled = true;

		//inputs
		configInput(PLAY_CV_INPUT, "Play");
		configInput(PLAYHEAD_CV_INPUT, "Playhead Mode");
		configInput(PAGE_CV_INPUT, "Page");
		configInput(DATA_ONE_INPUT, "Data 1");
		configInput(DATA_TWO_INPUT, "Data 2");
		configInput(STEPS_CV_INPUT, "Step Count");
		
		//outputs
		configOutput(DATA_ONE_OUTPUT, "Data 1");
		configOutput(DATA_TWO_OUTPUT, "Data 2");

		leftExpander.producerMessage = leftMessages[0];
		leftExpander.consumerMessage = leftMessages[1];
		rightExpander.producerMessage = rightMessages[0];
		rightExpander.consumerMessage = rightMessages[1];
	}

	void process(const ProcessArgs& args) override {
		//Edit button lights
		lights[COPY_LIGHT].setBrightness(params[COPY_BUTTON_PARAM].getValue());
		lights[CUT_LIGHT].setBrightness(params[CUT_BUTTON_PARAM].getValue());
		lights[PASTE_LIGHT].setBrightness(params[PASTE_BUTTON_PARAM].getValue());

		// Both main modules use the same expander messages
		const bool is_momma = rightExpander.module && (rightExpander.module->model == modelSixtyFourGatePitchSeq
			|| rightExpander.module->model == modelSixtyFourGatePitchSeqKnobs);
		
		// Read right expander message
		if (is_momma) {
			float* rightMessage = (float*)rightExpander.consumerMessage;
			//[0] - Data One
			expanderSignalDataOne = rightMessage[0];
			//[1] - Data Two
			expanderSignalDataTwo = rightMessage[1];
		}
		// Send right expander message
		if (is_momma) {
			float* rightMessage = (float*)rightExpander.module->leftExpander.producerMessage;
			//[0] - Play
			rightMessage[0] = inputs[PLAY_CV_INPUT].isConnected() ? inputs[PLAY_CV_INPUT].getVoltage() : params[PLAY_PARAM].getValue();
			//[1] - Page
			float pageVal = 0.f;
			if(inputs[PAGE_CV_INPUT].isConnected()){
				float pageVoltage = inputs[PAGE_CV_INPUT].getVoltage();
				pageVal = pageVoltage < 1.f ? 1.f : pageVoltage;
				pageVal = pageVal > 10.f ? 10.f : pageVal;
			} else {
				pageVal = params[PAGE_KNOB_PARAM].getValue();
			}
			rightMessage[1] = pageVal;
			//[2] - Playhead
			float playheadVal = 0.f;
			if(inputs[PLAYHEAD_CV_INPUT].isConnected()){
				playheadVal = inputs[PLAYHEAD_CV_INPUT].getVoltage() / 3.3333;
				playheadVal = playheadVal < 0.f ? 0.f : playheadVal;
				playheadVal = playheadVal > 2.f ? 2.f : playheadVal;
			} else{
				playheadVal = (params[PLAYHEAD_KNOB_PARAM].getValue() - 1.f);
			}
			rightMessage[2] = playheadVal;
			//[3] - Data One
			rightMessage[3] = inputs[DATA_ONE_INPUT].getVoltage();
			//[4] - Data Two
			rightMessage[4] = inputs[DATA_TWO_INPUT].getVoltage();
			//[5] - Step CV
			rightMessage[5] = inputs[STEPS_CV_INPUT].isConnected() ? inputs[STEPS_CV_INPUT].getVoltage() : -1.f;
			//[6] - Copy Button
			rightMessage[6] = params[COPY_BUTTON_PARAM].getValue();
			//[7] - Cut Button
			rightMessage[7] = params[CUT_BUTTON_PARAM].getValue();
			//[8] - Paste Button
			rightMessage[8] = params[PASTE_BUTTON_PARAM].getValue();
			//[9] - One Shot
			rightMessage[9] = params[ONESHOT_PARAM].getValue();

			rightExpander.module->leftExpander.messageFlipRequested = true;
		}
		// Set Output Voltages
		//[0] DATA 1
		outputs[DATA_ONE_OUTPUT].setVoltage(expanderSignalDataOne);

		//[1] DATA 2
		outputs[DATA_TWO_OUTPUT].setVoltage(expanderSignalDataTwo);

		lights[PLAY_LIGHT].setBrightness(inputs[PLAY_CV_INPUT].isConnected() ? (inputs[PLAY_CV_INPUT].getVoltage() > 0.f ? 1.f : 0.f) : params[PLAY_PARAM].getValue());
		lights[ONESHOT_LIGHT].setBrightness(params[ONESHOT_PARAM].getValue());
	}
};


struct SixtyFourGatePitchSeqExpanderWidget : ModuleWidget {
	SixtyFourGatePitchSeqExpanderWidget(SixtyFourGatePitchSeqExpander* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/n_SixtyFourGatePitchSeqExpander.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		float PortY[8] = {0};
		const float inputPortSpacing = 14.77f;
		const float inputPortStartHeight = 13.0f;
		for (int i = 0; i < 8; i++) {
			PortY[i] = inputPortStartHeight + inputPortSpacing * i;
		}

		// Buttons
		//[0] - Play
		addParam(createLightParamCentered<LightButton<MidSizeButton, MidsizeButtonCenterLight<WhiteLight>>>(mm2px(Vec(7.16, PortY[1])), module, SixtyFourGatePitchSeqExpander::PLAY_PARAM, SixtyFourGatePitchSeqExpander::PLAY_LIGHT));
		//[0.5] - One Shot
		addParam(createLightParamCentered<LightButton<MidSizeButton, MidsizeButtonCenterLight<WhiteLight>>>(mm2px(Vec(18.24, PortY[1])), module, SixtyFourGatePitchSeqExpander::ONESHOT_PARAM, SixtyFourGatePitchSeqExpander::ONESHOT_LIGHT));
		//[1] - Copy
		addParam(createLightParamCentered<LightButton<MomentaryMidSizeButton, MidsizeButtonCenterLight<WhiteLight>>>(mm2px(Vec(7.16, PortY[4])), module, SixtyFourGatePitchSeqExpander::COPY_BUTTON_PARAM, SixtyFourGatePitchSeqExpander::COPY_LIGHT));
		//[2] - Cut
		addParam(createLightParamCentered<LightButton<MomentaryMidSizeButton, MidsizeButtonCenterLight<WhiteLight>>>(mm2px(Vec(18.24, PortY[4])), module, SixtyFourGatePitchSeqExpander::CUT_BUTTON_PARAM, SixtyFourGatePitchSeqExpander::CUT_LIGHT));
		//[3] - Paste
		addParam(createLightParamCentered<LightButton<MomentaryMidSizeButton, MidsizeButtonCenterLight<WhiteLight>>>(mm2px(Vec(7.16, PortY[5])), module, SixtyFourGatePitchSeqExpander::PASTE_BUTTON_PARAM, SixtyFourGatePitchSeqExpander::PASTE_LIGHT));

		// Knobs
		//[0] - Page
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(7.16, PortY[3])), module, SixtyFourGatePitchSeqExpander::PAGE_KNOB_PARAM));
		//[1] - Playhead
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(18.24, PortY[3])), module, SixtyFourGatePitchSeqExpander::PLAYHEAD_KNOB_PARAM));

		// Inputs
		//[0] - Play CV
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(7.16, PortY[0])), module, SixtyFourGatePitchSeqExpander::PLAY_CV_INPUT));
		//[1] - Page CV
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(7.16, PortY[2])), module, SixtyFourGatePitchSeqExpander::PAGE_CV_INPUT));
		//[2] - Playhead CV
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(18.24, PortY[2])), module, SixtyFourGatePitchSeqExpander::PLAYHEAD_CV_INPUT));
		//[3] - DATA 1
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(7.16, PortY[6])), module, SixtyFourGatePitchSeqExpander::DATA_ONE_INPUT));
		//[4] - DATA 2 (MOVED to original DATA 1 OUTPUT position)
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(18.24, PortY[6])), module, SixtyFourGatePitchSeqExpander::DATA_TWO_INPUT));
		//[5] - Steps CV
		addInput(createInputCentered<DarkPJ301MPort>(mm2px(Vec(18.24, PortY[0])), module, SixtyFourGatePitchSeqExpander::STEPS_CV_INPUT));

		// Outputs
		//[0] - DATA 1 (MOVED to original DATA 2 INPUT position)
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(7.16, PortY[7])), module, SixtyFourGatePitchSeqExpander::DATA_ONE_OUTPUT));
		//[1] - DATA 2
		addOutput(createOutputCentered<DarkPJ301MPort>(mm2px(Vec(18.24, PortY[7])), module, SixtyFourGatePitchSeqExpander::DATA_TWO_OUTPUT));
	}
};


Model* modelSixtyFourGatePitchSeqExpander = createModel<SixtyFourGatePitchSeqExpander, SixtyFourGatePitchSeqExpanderWidget>("SixtyFourGatePitchSeqExpander");
