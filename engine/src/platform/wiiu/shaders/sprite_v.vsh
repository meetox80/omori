; $MODE = "UniformRegister"
; $ATTRIB_VARS[0].name = "aPosition"
; $ATTRIB_VARS[0].type = "vec2"
; $ATTRIB_VARS[0].location = 0
; $ATTRIB_VARS[1].name = "aTexcoord"
; $ATTRIB_VARS[1].type = "vec2"
; $ATTRIB_VARS[1].location = 1
; $ATTRIB_VARS[2].name = "aColor"
; $ATTRIB_VARS[2].type = "vec4"
; $ATTRIB_VARS[2].location = 2
; $ATTRIB_VARS[3].name = "aExtra"
; $ATTRIB_VARS[3].type = "vec4"
; $ATTRIB_VARS[3].location = 3
; $UNIFORM_VARS[0].name = "uTransform"
; $UNIFORM_VARS[0].type = "vec4"
; $UNIFORM_VARS[0].count = 1
; $UNIFORM_VARS[0].offset = 0
; $UNIFORM_VARS[0].block = -1
; $SPI_VS_OUT_CONFIG.VS_EXPORT_COUNT = 2
; $NUM_SPI_VS_OUT_ID = 1
; $SPI_VS_OUT_ID[0].SEMANTIC_0 = 0
; $SPI_VS_OUT_ID[0].SEMANTIC_1 = 1
; $SPI_VS_OUT_ID[0].SEMANTIC_2 = 2

00 CALL_FS NO_BARRIER
01 ALU:
   0  x: MULADD R1.x, R1.x, C0.x, C0.z
      y: MULADD R1.y, R1.y, C0.y, C0.w
      z: MOV R1.z, 0.5f
      w: MOV R1.w, 1.0f
02 EXP_DONE: POS0, R1.xyzw
03 EXP: PARAM0, R2.xy00 NO_BARRIER
04 EXP: PARAM1, R3.xyzw NO_BARRIER
05 EXP_DONE: PARAM2, R4.xyzw NO_BARRIER
END_OF_PROGRAM
