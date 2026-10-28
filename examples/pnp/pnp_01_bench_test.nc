(dpCREATOR PnP beta - 01 bench test)
(Run from the SD card AFTER homing. Nozzle empty, nothing on the bed.)
(Each step stops with M0. Watch the message, check the result,)
(write it in the test log, then press Resume / Cycle Start.)
(Z 0 = top. Safe height below is -2 mm from the top.)

#<safe_z> = -2

G21 G90 G94
G0 Z#<safe_z>

(MSG, STEP 1 - Pump ON. Expect: pump runs. Press Resume.)
M62 P0
M0

(MSG, STEP 2 - Valve ON. Expect: valve clicks, suction at the nozzle tip. Press Resume.)
M62 P1
M0

(MSG, STEP 3 - Cover the nozzle tip with a finger now, then press Resume.)
M0
G4 P0.3
M66 P0 L0
(PRINT, Vacuum with tip covered = #5399  - expect 1)
M0

(MSG, STEP 4 - Uncover the nozzle tip, then press Resume.)
M0
G4 P0.3
M66 P0 L0
(PRINT, Vacuum with tip open = #5399  - expect 0)
M0

(MSG, STEP 5 - Valve OFF. Expect: valve clicks, nozzle vents, no suction. Press Resume.)
M63 P1
M0

(MSG, STEP 6 - Nozzle turns +90, back to 0, -90, back to 0. Press Resume to start.)
M0
G0 A90
G4 P0.5
G0 A0
G4 P0.5
G0 A-90
G4 P0.5
G0 A0
(MSG, STEP 6 done - did the nozzle return to exactly the same angle? Press Resume.)
M0

(MSG, STEP 7 - Ten full turns and back. Mark the nozzle first. Press Resume to start.)
M0
G0 A3600
G0 A0
(MSG, STEP 7 done - is the mark where it started? If not, A is losing steps. Press Resume.)
M0

M63 P1
M63 P0
(MSG, Bench test finished - pump and valve OFF.)
M30
