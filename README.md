<h1>
KWA Modules for VCV Rack 2
</h1> 

A collection of sequencer modules for VCV Rack 2

  ![Showcase](doc/showcase.png)

<h2>KWA Pitch 64</h2>
64 step pitch sequencer with voct / velocity recording

- **Record** - When gate input is received, records voct / velocity inputs to the current 4x clock quantized
   step
  
  ![Record_mode](doc/record_mode.gif)
  
- **Edit** - Select multiple steps and set them to input voct / velocity when an input gate is received. When one step is selected, record inputs step by step when gate input is triggered.

  ![Edit mode](doc/edit_mode.gif)
  
- **Gates** - Erases data from a step, or marks that a trigger signal should be there
  
  ![Gate mode](doc/gate_mode.gif)
  
- **Display knob** - Switches between display modes 
	- Gates,
	- V/Oct (normalized red to green)
	- Velocity (normalized dim red to bright red)
- **Step Knob** Selects number of steps the sequencer should step through


<h2> KWA Pitch 64 Knobs </h2>

The KWA Pitch 64 with the step buttons replaced by knob / button hybrids. Same ports, same
three modes, same expander messages - only the 64 square buttons became knobs.

- **Knob** Turn it to write into the step it addresses. The knob position always shows the value currently stored on that step.
- **Step button** The rubber button at the bottom right of each cell is a half size version of the KWA Control 8 gate button. Like the KWA Pitch 64 it is a momentary button, not a toggle - the module owns the toggling, so in **Gates** mode a click only mutes or unmutes that one step, and in **Edit** mode it only selects or deselects that one step. Step state and the display colours live on this button, the knob has no light in it.
- **Muting** Muting a step in **Gates** mode only takes its trigger away, it never erases the note. Unmuting brings back exactly the V/Oct, velocity and data that were stored, and the knob still shows the value throughout. A muted step's light goes completely dark, and both its knob and its step button are washed out, so the whole cell reads as off at a glance.
- **Two dim levels** Inactive steps wash out at two strengths, so the Steps knob's range is readable straight off the matrix. A step past the Steps knob is washed almost to black, and a step inside the range whose trigger is muted is washed to about half that. Either way the pointer still shows where the value sits.

Works with the KWA Pitch 64 Expander on either side, the same way the KWA Pitch 64 does - **Run**, **One Shot**, **Step Mode**, **Page**, **Step Count** CV and the **Copy** / **Cut** / **Paste** buttons all behave as they do with the original. The expander's **Data 1** / **Data 2** outputs carry the data buses of the step that is playing.
- **Playhead** A green glow behind a step button marks the step that is playing.
- **Display knob** Decides what the knobs address, and re-snap all 64 knobs to that source the moment you change it
  - **Probability** - each knob is a chance of that step firing, 1 is always and 0 is never, linear in between. Blue brightness shows the chance. These knobs always run 0 to 10, whatever the Knob Range is set to
  - **V/Oct** - knobs address V/Oct, LEDs go red at the bottom of the range to green at the top
  - **Velocity** - knobs address velocity, LEDs go dim red to bright red
  - **Data 1** / **Data 2** - knobs address the data buses (cyan and magenta), which are also what the expander's data outputs send
- **Every step starts on.** A fresh module, and Rack's initialize, leaves all 64 steps enabled with a note slot ready and a chance of firing, so it runs as soon as you patch a clock and a gate. Mute the steps you don't want, or pull the Steps knob down to only play the first few.
- **Probability** A step fires with a chance of `knob / 10`, so a knob at full brightness is a certainty and one at zero never fires. The roll happens once per trigger on the clock and on reset. One probability per step is shared by every voice. While the display is on Probability the knob tooltips read as a percentage, and you can type one into the context menu, rather than as volts.
- **Colours are absolute** A value always gets the same colour, no matter what else is on the matrix. Nothing is normalised against the other steps, so two patches never disagree about what a colour means.
- **Knob Range** Right click the module to switch the step knobs between -10 to 10 (the default, for bipolar signals) and 0 to 10. Velocity and the data buses are unipolar, so switch to 0 to 10 when editing those. The range only rescales the colours, never the knob positions - turning a knob always writes the value its pointer is on.
- **Recording** Identical to the button version. Arm **Record**, the gate writes onto the step the playhead is on, and the knob of that step moves to whatever arrived on the patched inputs.

Knobs read and write the first channel of each step, so with a mono signal every step is directly editable.


<h2> KWA Control 8 </h2>

Expandable 8 step trigger / drum sequencers

![Control 8](doc/control_8.gif)

 <h3> Control 8 Manager </h3>
 
**Ctrl 8's main controller** - Signals chain to all connected control 8 sequencers, continuous mode steps through each chained sequencer sequentially.
 <h3> Control 8 </h3>
 
 **Ctrl 8's sequencer modules** - any number can be chained and run in parallel or stepped through sequentially if manager's continuous mode button is pressed.
 <h3> Control 8 Monitor </h3>
 
 **Ctrl 8's chained outputs** - Contains a global output for any stepped trigger, as well as various other useful chain outputs

 
<h2>KWA Trig 8</h2>

Simplified Control 8 sequencer with standalone clock / reset input, compatible with all KWA Control 8 expanders / sequencers.

![Trig 8](doc/trig_8.gif)