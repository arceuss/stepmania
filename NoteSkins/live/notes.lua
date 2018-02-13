local skin_name= Var("skin_name")
return function(button_list, stepstype)
	local tap_redir= {
		Fret1= "White", Fret2= "White", Fret3= "White", Fret4= "Black", 
		Fret5= "Black", Fret6= "Black", StrumDown= "Strum"
	}
	local tap_width= {
		-- spacing is especially weird due to how the columns are supposed to line up
		Fret1= 36, Fret2= 36, Fret3= 36, Fret4= 36, 
		Fret5= 36, Fret6= 36, StrumDown= 1
	}
	local hold_redir= {
		Fret1= "Fret", Fret2= "Fret", Fret3= "Fret", Fret4= "Fret", 
		Fret5= "Fret", Fret6= "Fret", StrumDown= "Strum"
	}
	local col_position= {
		Fret1= -72, Fret2= 0, Fret3= 72, Fret4= -72, 
		Fret5= 0, Fret6= 72, StrumDown= 0
	}
	local parts_per_beat= 48
	local tap_state_map= {
		parts_per_beat= parts_per_beat, quanta= {
			{per_beat= 1, states= {1}}, -- One state to rule them all
		},
	}
	local lift_state_map= {
		parts_per_beat= parts_per_beat, quanta= {
			{per_beat= 1, states= {1}}, -- One state to find them
		},
	}
	local hold_length= {
		start_note_offset= -.5,
		end_note_offset= .5,
		head_pixs= 32,
		body_pixs= 64,
		tail_pixs= 32
	}
	-- Mines only have a single frame in the graphics.
	local mine_state_map= {
		parts_per_beat= 1, quanta= {
			{per_beat= 1, states= {1}}, -- One state to bring them all
		},
	}
	local active_state_map= {
		parts_per_beat= parts_per_beat, quanta= {
			{per_beat= 1, states= {1}}, -- And in the darkness
		},
	}
	local inactive_state_map= {
		parts_per_beat= parts_per_beat, quanta= {
			{per_beat= 1, states= {2}}, -- Bind them
		},
	}
	local columns= {}
	for i, button in ipairs(button_list) do
		local hold_tex= hold_redir[button].." Hold 2x1.png"
		local roll_tex= hold_redir[button].." Roll 2x1.png"
		columns[i]= {
			width= tap_width[button],
			anim_time= 1,
			anim_uses_beats= true,
			padding= 0,
			-- new thing to line up columns
			custom_x= col_position[button],
			taps= {
				NewSkinTapPart_Tap= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= tap_redir[button].." Tap Note.png",
					InitCommand= function(self) self:rotationz(0) end}
					},
				NewSkinTapPart_Mine= {
					state_map= mine_state_map,
					actor= Def.Sprite{Texture= "mine.png"}},
				NewSkinTapPart_Lift= { -- fuck lifts
					state_map= lift_state_map,
					actor= Def.Sprite{Texture= tap_redir[button].." Tap HOPO.png",
						InitCommand= function(self) self:rotationz(0) end}},
				NewSkinTapPart_Gem= {
					state_map= tap_state_map,
					actor= Def.Sprite{Texture= tap_redir[button].." Tap Gem.png",
						InitCommand= function(self) self:rotationz(0) end}},
				NewSkinTapPart_HOPO= {
					state_map= lift_state_map,
					actor= Def.Sprite{Texture= tap_redir[button].." Tap HOPO.png",
						InitCommand= function(self) self:rotationz(0) end}},
			},
			holds= {
				TapNoteSubType_Hold= {
					{
						state_map= inactive_state_map,
						textures= {hold_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
					{
						state_map= active_state_map,
						textures= {hold_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
				},
				TapNoteSubType_Roll= {
					{
						state_map= inactive_state_map,
						textures= {roll_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
					{
						state_map= active_state_map,
						textures= {roll_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
				},
			},
			reverse_holds= {
				TapNoteSubType_Hold= {
					{
						state_map= inactive_state_map,
						textures= {hold_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
					{
						state_map= active_state_map,
						textures= {hold_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
				},
				TapNoteSubType_Roll= {
					{
						state_map= inactive_state_map,
						textures= {roll_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
					{
						state_map= active_state_map,
						textures= {roll_tex},
						flip= TexCoordFlipMode_None,
						length_data= hold_length,
					},
				},
			},
		}
	end
	return {
		columns= columns,
		vivid_operation= true, -- output 200%
	}
end
