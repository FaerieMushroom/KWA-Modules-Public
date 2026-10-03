#include "KWAModules.hpp"


struct SixtyFourGatePitchSeq : Module {
	enum ParamIds {
		ENUMS(BUTTON_MATRIX_PARAMS, 64),
		RECORD_MODE_PARAM,
		EDIT_MODE_PARAM,
		GATE_MODE_PARAM,
		DISPLAY_MODE_PARAM,
		STEP_COUNT_PARAM,
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
		ENUMS(ACTIVE_STEP_LIGHTS, 64),
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
	bool shouldRefreshDisplay = false;
	bool wasButtonPressedThisSample = false;
	bool wasInitialized = false;
	bool anyPressed = false;
	bool gateModifiedSinceRelease = false;
	bool copyPressedDown = false;
	bool cutPressedDown = false;
	bool pastePressedDown = false;

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

	SixtyFourGatePitchSeq();

	void process(const ProcessArgs& args) override;

	void processFifty(const ProcessArgs& args, int channels);

	void setModeBrightnesses();
	
	void updateDisplay(int channels);

	void clearNoteParams(int i, int channel);

	json_t* dataToJson() override;

	void dataFromJson(json_t* rootJ) override;
	
	void onReset(const ResetEvent& e) override;

	void onRandomize(const RandomizeEvent& e) override;

	void copyNotes(int channels, bool cut = false);

	void cutNotes(int channels);

	void pasteNotes(int channels);
	
};

struct SixtyFourGatePitchSeqWidget : ModuleWidget {
	SixtyFourGatePitchSeqWidget(SixtyFourGatePitchSeq* module);
};
