(dpCREATOR PnP beta - 02 pick and place ONE part)
(Template: fill in the numbers below, run from the SD card after homing.)
(Positions are MACHINE coordinates - jog the nozzle tip to each spot and)
(copy the MPos values from the WebUI or pendant.)

(---------- SET THESE ----------)
#<safe_z>   = -2       (travel height, nozzle tip clear of everything)
#<pick_x>   = 50.00    (centre of the part in the feeder pocket)
#<pick_y>   = 50.00
#<pick_z>   = -20.00   (nozzle tip just touching the part)
#<place_x>  = 150.00   (centre of the pads on the board)
#<place_y>  = 100.00
#<place_z>  = -19.00   (nozzle tip at part height above the board)
#<place_a>  = 90       (part rotation on the board, degrees)
#<z_feed>   = 600      (mm/min for the last mm down)
(--------------------------------)

G21 G90 G94
G53 G0 Z#<safe_z>
G53 G0 A0

(Pump on and give it a moment to build vacuum)
M62 P0
G4 P0.5

(--- PICK ---)
G53 G0 X#<pick_x> Y#<pick_y>
G53 G0 Z[#<pick_z> + 1]
G53 G1 Z#<pick_z> F#<z_feed>
M62 P1                 (valve on - waits until Z is down)
G4 P0.15               (let the vacuum grip)
G53 G0 Z#<safe_z>

(--- CHECK THE PART IS ON THE NOZZLE ---)
(No vacuum switch fitted? Delete from here to "O101 ENDIF".)
G4 P0.05
M66 P0 L0
#<vac> = #5399
O100 IF [#<vac> EQ 0]
  M63 P1
  (MSG, MISSED PICK - no vacuum. Fix the feeder or part, then press Resume to try again.)
  M0
  G53 G0 X#<pick_x> Y#<pick_y>
  G53 G0 Z[#<pick_z> + 1]
  G53 G1 Z#<pick_z> F#<z_feed>
  M62 P1
  G4 P0.15
  G53 G0 Z#<safe_z>
  G4 P0.05
  M66 P0 L0
  #<vac> = #5399
O100 ENDIF
O101 IF [#<vac> EQ 0]
  (MSG, STILL NO PART - press Stop to end the job, or Resume to continue anyway.)
  M0
O101 ENDIF

(--- PLACE ---)
G53 G0 X#<place_x> Y#<place_y> A#<place_a>
G53 G0 Z[#<place_z> + 1]
G53 G1 Z#<place_z> F#<z_feed>
M63 P1                 (valve off - waits until Z is down, nozzle vents)
G4 P0.1                (let the part release)
G53 G0 Z#<safe_z>
G53 G0 A0

M63 P0                 (pump off)
(MSG, Part placed.)
M30
