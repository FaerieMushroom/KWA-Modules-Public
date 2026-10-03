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
		COLOUR_MODE_PARAM,
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

	//State vars. Mono by design, so these are page then step - there is no voice axis.
	float voctPCV[10][64] = {0.f};
	float velocityPCV[10][64] = {0.f};
	float gatePCV[10][64] = {0.f};
	float hasRecordedDataPCV[10][64] = {0.f};
	float dataOnePCV[10][64] = {0.f};
	float dataTwoPCV[10][64] = {0.f};
	// Chance a step fires, stored on the same 0..10 scale as everything else so 10.f is
	// always and 0.f is never. Defaults to always.
	float probabilityPCV[10][64];

	float voctCopy = 0.f;
	float velocityCopy = 0.f;
	bool gateCopy = false;
	bool hasRecordedDataCopy = false;
	float dataOneCopy = 0.f;
	float dataTwoCopy = 0.f;
	bool selectionCopy = false;

	int currentStep = 0;
	int currentSubstep = 0;
	int currentWorkingStep = 0;
	int processCounter = 0;
	int clockStepsSinceLastEdit = 0;
	int editCount = 0;
	int sequencePage = 0;
	int stepCountInt = 0;
	int pingPongDir = 1;
	// Probability roll result for the step the playhead is on, for the playhead light
	bool currentStepFired = true;

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

	float leftMessages[2][10] = {};
	float rightMessages[2][10] = {};

	struct Engine {
		float voctVoltage = 0.f;
		bool hasTriggerOutputPulsedThisStep = false;
		bool isGateHigh = false;
		bool gateFellThisSample = false;
		bool gateRoseThisSample = false;
		bool voctMovedThisSample = false;
		// The step the cached probability roll belongs to, -1 when nothing is cached.
		// Rolling once per step and remembering it is what keeps the trigger and any data
		// written for that step agreeing on the same result.
		int rolledStep = -1;
		bool rolledResult = false;
		dsp::PulseGenerator triggerOutputPulse;
	};
	Engine engine;

	SixtyFourGatePitchSeqKnobs();

	void process(const ProcessArgs& args) override;

	void processFifty(const ProcessArgs& args);

	void setModeBrightnesses();

	void updateDisplay();

	void clearNoteParams(int i);

	/** Value the step knobs currently address: 1 and 2 edit V/Oct, 3 velocity, 4/5 the data buses. */
	int getDataSource();

	float getKnobMin();

	/** Bottom of the step knob range. Probability is always 0 to 10, whatever the
	context menu range is set to. */
	float knobRangeMin();

	/** Rolls the step's trigger. A step fires with a chance of probability / 10. */
	bool rollTrigger(int step);

	/** Whether a step fires: it has to be enabled, and it has to pass its probability
	roll. Rolled once per step and cached, so the trigger output and any data written for
	that step always share one result. A step clicked off never fires. */
	bool stepFires(int step);

	/** Forgets the cached rolls, so a changed probability or gate takes effect at once. */
	void invalidateStepRolls();

	/** Slot that the step knobs read from and write to. */
	float* stepValuePtr(int i);

	void syncKnob(int i);

	void syncKnobs();

	void updateKnobLabels();

	void pullKnobs();

	/** The 64 step values a display source addresses on the current page, or NULL for an
	unknown source. Sources match getDataSource(): 1 probability, 2 V/Oct, 3 velocity, 4 and 5
	the data buses. The arrays are contiguous, so one pointer covers the whole page. */
	float* stepDataPtr(int source);

	/** Randomises one source across every step of the current page. */
	void randomizeStepData(int source);

	/** Resets one source across every step of the current page. */
	void initializeStepData(int source);

	json_t* dataToJson() override;

	void dataFromJson(json_t* rootJ) override;

	void onReset(const ResetEvent& e) override;

	void onRandomize(const RandomizeEvent& e) override;

	void copyNotes(bool cut = false);

	void cutNotes();

	void pasteNotes();

};


struct SixtyFourGatePitchSeqKnobsWidget : ModuleWidget {
	SixtyFourGatePitchSeqKnobsWidget(SixtyFourGatePitchSeqKnobs* module);
	void appendContextMenu(ui::Menu* menu) override;
};
