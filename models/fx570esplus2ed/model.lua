do
	local buttons = {}
	local function generate(px, py, w, h, nx, ny, sx, sy, code) --px,py=first button pos x and y, | w, h=width and height of button | nx,ny - number of buttons horizontal an vertical | sx,sy=distance between buttons | code = id, code+1=kbd //all values in px
		local cp = 1
		for iy = 0, ny - 1 do
			for ix = 0, nx - 1 do
				table.insert(buttons, {px + ix*sx, py + iy*sy, w, h, code[cp], code[cp+1]})
				cp = cp + 2
			end
		end
	end
	-- Refer to https://wiki.libsdl.org/SDL_Keycode for key names.
	local le = 46 --left edge
	local sbw = 47 -- small button width
	local sbh = 28 -- small button width

	generate(142,327,  sbw,sbh,   2,1,  77,  0, {0x26, 'Left', 0x37, 'Right',})
	generate(190,293,  sbh,sbw,   1,2,   0, 50, {0x27, 'Up',   0x36, 'Down',})

	generate( 51,300,   30,30,   1,1,   0,0, {0x07, 'F1', }) -- [SHIFT] 
	generate(102,300,   30,30,   1,1,   0,0, {0x17, 'F2', }) -- [ALPHA]
	generate(275,300,   30,30,   1,1,   0,0, {0x47, 'F3', }) -- [MODE]
	generate(326,300,   30,30,   1,1,   0,0, {0xFF, 'F4', }) -- [ON]


	generate( le,366, sbw,sbh, 2,1,  54,0,   {0x06, 'F5',   0x16, 'F6',}) -- [CALC] [INTEGRAL]
	generate(262,366, sbw,sbh, 2,1,  54,0,   {0x46, 'F7',   0x56, 'F8',}) -- [X^-1] [log_a_b]

	generate(le,413,  sbw,sbh,  6,3, 54,47, {
		0x05, '', 0x15, '', 0x25, '', 0x35, '', 0x45, '', 0x55, '',
		0x04, '', 0x14, '', 0x24, '', 0x34, '', 0x44, '', 0x54, '',
		0x03, '', 0x13, '', 0x23, '', 0x33, '', 0x43, '', 0x53, '',


	generate(le,560,  55,39, 5,4, 66,57, {
		0x02, '7', 0x12, '8', 0x22, '9', 0x32, 'Backspace', 0x42, 'Space',
		0x01, '4', 0x11, '5', 0x21, '6', 0x31, '' ,         0x41, '/',
		0x00, '1', 0x10, '2', 0x20, '3', 0x30, '=',         0x40, '-',
		0x64, '0', 0x63, '.', 0x62, 'E', 0x61, '' ,         0x60, 'Return',
	})
	})

	emu:model({
		model_name = "fx-570ES PLUS 2nd edition",
		interface_image_path = "interface.png",
		rom_path = "rom.bin",
		hardware_id = 3,
		real_hardware = 1,
		csr_mask = 0x0001,
		pd_value = 0x00,
		rsd_interface = {0, 0, 410, 810, 0, 0},
		rsd_pixel = {410, 252,  3,  3,  61, 141},
		rsd_s     = {410,   0, 10, 14,  61, 127},
		rsd_a     = {410,  14, 11, 14,  70, 127},
		rsd_m     = {410,  28, 10, 14,  81, 127},
		rsd_sto   = {410,  42, 20, 14,  91, 127},
		rsd_rcl   = {410,  56, 19, 14, 110, 127},
		rsd_stat  = {410,  70, 24, 14, 130, 127},
		rsd_cmplx = {410,  84, 32, 14, 154, 127},
		rsd_mat   = {410,  98, 20, 14, 186, 127},
		rsd_vct   = {410, 112, 20, 14, 205, 127},
		rsd_d     = {410, 126, 12, 14, 225, 127},
		rsd_r     = {410, 140, 10, 14, 236, 127},
		rsd_g     = {410, 154, 11, 14, 246, 127},
		rsd_fix   = {410, 168, 17, 14, 257, 127},
		rsd_sci   = {410, 182, 16, 14, 273, 127},
		rsd_math  = {410, 196, 24, 14, 289, 127},
		rsd_down  = {410, 210, 10, 14, 313, 127},
		rsd_up    = {410, 224, 10, 14, 319, 127},
		rsd_disp  = {410, 238, 20, 14, 329, 127},
		ink_colour = {49, 49, 49},
		button_map = buttons
	})
end
