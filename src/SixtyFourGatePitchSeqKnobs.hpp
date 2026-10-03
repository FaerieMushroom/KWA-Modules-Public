#include "KWAModules.hpp"


/** The KWA Pitch 64 with the step buttons replaced by knob / button hybrids.
Everything else - modes, ports, expander messages - matches SixtyFourGatePitchSeq.
*/
struct SixtyFourGatePitchSeqKnobs : Module {
	enum ParamIds {
		ENUMS(STEP_KNOB_PARAMS, 64),
		ENUMS(STEP_BUTTON_PARAMS, 64),
		RECORD_MODE_PARAM,
		EDIT_MODE_PARAM,
		GATE_MODE_PARAM,
		DISPLAY_MODE_PARAM,
		STEP_COUNT_PARAM,
		KNOB_RANGE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		GATE_INPUT,
		VOCT_INPUT,
		VELOCITY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		VOCT_OUTPUT,
		VELOCITY_OUTPUT,
		CLOCK_OUTPUT,
		RESET_OUTPUT,
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(STEP_LIGHTS, 192),
		ENUMS(PLAYHEAD_LIGHTS, 64),
		RECORD_MODE_LIGHT,
		EDIT_MODE_LIGHT,
		GATE_MODE_LIGHT,
		NUM_LIGHTS
	};
	//Input Schmitt Triggers
	dsp::SchmittTrigger clockInputSchmitt;
	dsp::SchmittTrigger resetInputSchmitt;

	//Signal Output Pulses
	dsp::PulseGenerator clockOutputPulse;
	dsp::PulseGenerator eocOutputPulse;

	//State vars
	float voctPCV[10][16][64] = {0.f};
	float velocityPCV[10][16][64] = {0.f};
	float gatePCV[10][16][64] = {0.f};
	float hasRecordedDataPCV[10][16][64] = {0.f};
	float dataOnePCV[10][16][64] = {0.f};
	float dataTwoPCV[10][16][64] = {0.f};

	float voctCopy[16] = {0.f};
	float velocityCopy[16] = {0.f};
	bool gateCopy[16] = {false};
	bool hasRecordedDataCopy[16] = {false};
	float dataOneCopy[16] = {0.f};
	float dataTwoCopy[16] = {0.f};
	bool selectionCopy[16] = {false};

	int currentStep = 0;
	int currentSubstep = 0;
	int currentWorkingStep = 0;
	int processCounter = 0;
	int clockStepsSinceLastEdit = 0;
	int editCount = 0;
	int sequencePage = 0;
	int stepCountInt = 0;
	int pingPongDir = 1;

	bool gateModeSelected = false;
	bool isRecording = false;
	bool editModeSelectedGates[64] = {false};
	bool anyPressed = false;
	bool gateModifiedSinceRelease = false;
	bool shouldRefreshDisplay = false;
	bool wasButtonPressedThisSample = false;
	bool wasInitialized = false;
	bool copyPressedDown = false;
	bool cutPressedDown = false;
	bool pastePressedDown = false;

	//Knob bookkeeping
	float knobEcho[64] = {0.f};
	// How much each part of an inactive step is washed out, 0 = full brightness. These
	// mirror whether the step is lit, so a cell reads as active or not at a glance.
	// The step button is barely half the width of the knob, so its mid level has to sit
	// darker than the knob's to stay readable.
	float knobShade[64] = {0.f};
	float buttonShade[64] = {0.f};
	int knobRangeMode = 0;
	int lastDisplayMode = -1;

	float expanderSignalPlay = 0.f;
	float expanderSignalOneShot = 0.f;
	float expanderSignalPage = 0.f;
	float expanderSignalPlayhead = 0.f;
	float expanderSignalDataOne = 0.f;
	float expanderSignalDataTwo = 0.f;
	float expanderSignalSteps = 0.f;
	float expanderSignalCopy = 0.f;
	float expanderSignalCut = 0.f;
	float expanderSignalPaste = 0.f;
	float maxVoct = -10.f;
	float minVoct = 10.f;
	float maxVel = -10.f;
	float minVel = 10.f;
	float maxDataOne = -10.f;
	float minDataOne = 10.f;
	float maxDataTwo = -10.f;
	float minDataTwo = 10.f;

	float leftMessages[2][10] = {};
	float rightMessages[2][10] = {};

	struct Engine {
		float voctVoltage = 0.f;
		bool hasTriggerOutputPulsedThisStep = false;
		bool isGateHigh = false;
		bool gateFellThisSample = false;
		bool gateRoseThisSample = false;
		bool voctMovedThisSample = false;
		dsp::PulseGenerator triggerOutputPulse;
	};
	Engine engines[16];

	SixtyFourGatePitchSeqKnobs();

	void process(const ProcessArgs& args) override;

	void processFifty(const ProcessArgs& args, int channels);

	void setModeBrightnesses();

	void updateDisplay(int channels);

	void clearNoteParams(int i, int channel);

	/** Value the step knobs currently address: 1 and 2 edit V/Oct, 3 velocity, 4/5 the data buses. */
	int getDataSource();

	float getKnobMin();

	/** Channel 0 slot that the step knobs read from and write to. */
	float* stepValuePtr(int i);

	void syncKnob(int i);

	void syncKnobs();

	void updateKnobLabels();

	void pullKnobs();

	json_t* dataToJson() override;

	void dataFromJson(json_t* rootJ) override;

	void onReset(const ResetEvent& e) override;

	void onRandomize(const RandomizeEvent& e) override;

	void copyNotes(int channels, bool cut = false);

	void cutNotes(int channels);

	void pasteNotes(int channels);

};


struct SixtyFourGatePitchSeqKnobsWidget : ModuleWidget {
	SixtyFourGatePitchSeqKnobsWidget(SixtyFourGatePitchSeqKnobs* module);
	void appendContextMenu(ui::Menu* menu) override;
};


Model* modelSixtyFourGatePitchSeqKnobs = createModel<SixtyFourGatePitchSeqKnobs, SixtyFourGatePitchSeqKnobsWidget>("SixtyFourGatePitchSeqKnobs");
