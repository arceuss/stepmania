return function(button_list, stepstype, skin_params)
	local ret= {}
	local tap_redir= {
		Fret1= "Fret", Fret2= "Fret", Fret3= "Fret", Fret4= "Fret", 
		Fret5= "Fret", Fret6= "Fret", StrumDown= "Strum"
	}
	local x_reposition= {
		Fret1= 18, Fret2= 54, Fret3= 90, Fret4= -90, 
		Fret5= -54, Fret6= -18, StrumDown= -108
	}
	for i, button in ipairs(button_list) do
		local column_frame= Def.ActorFrame{
			InitCommand= function(self)
				self:rotationz(0)
					:draworder(newfield_draw_order.explosion)
			end,
			WidthSetCommand= function(self, param)
				param.column:set_layer_fade_type(self, "FieldLayerFadeType_Explosion")
			end,
			Def.ActorFrame{
				Def.Sprite{
					Texture= tap_redir[button].." Tap Explosion Dim.png",
					ColumnJudgmentCommand= function(self, param)
						if param.bright then
							self:visible(false)
						else
							self:visible(true)
						end
					end,
					HoldCommand= function(self, param)
						if param.finished then
							self:visible(false)
						else
							self:visible(true)
						end
					end,
				},
				Def.Sprite{
					Texture= tap_redir[button].." Tap Explosion Bright.png",
					ColumnJudgmentCommand= function(self, param)
						if param.bright then
							self:visible(true)
						else
							self:visible(false)
						end
					end,
					HoldCommand= function(self, param)
						if param.finished then
							self:visible(true)
						else
							self:visible(false)
						end
					end,
				},
				InitCommand= function(self)
					self:visible(false)
				end,
				ColumnJudgmentCommand= function(self, param)
					local diffuse= {
						-- it's gonna be TNS1 if its a hit anyway
						TapNoteScore_W1= {1, 1, 1, 1},
						TapNoteScore_W2= {1, 1, .3, 1},
						TapNoteScore_W3= {0, 1, .4, 1},
						TapNoteScore_W4= {.3, .8, 1, 1},
						TapNoteScore_W5= {.8, 0, .6, 1},
						HoldNoteScore_Held= {1, 1, 1, 1},
					}
					local exp_color= diffuse[param.tap_note_score or param.hold_note_score]
					if exp_color then
						self:stoptweening()
							:diffuse(exp_color):zoom(1):diffusealpha(1):visible(true)
							:linear(0.1):zoom(1.1):linear(0.06):diffusealpha(0)
							:sleep(0):queuecommand("hide")
					end
				end,
				hideCommand= function(self)
					self:visible(false)
				end,
			},
			Def.Sprite{
				Texture= tap_redir[button].." Hold Explosion 2x1.png", InitCommand= function(self)
					self:visible(false):SetAllStateDelays(.1)
				end,
				HoldCommand= function(self, param)
					if param.start then
						self:finishtweening()
							:zoom(1):diffusealpha(1):visible(true)
					elseif param.finished then
						self:stopeffect():linear(0.06):diffusealpha(0)
							:sleep(0):queuecommand("hide")
					else
						self:zoom(1)
					end
				end,
				hideCommand= function(self)
					self:visible(false)
				end,
			},
		}
		ret[i]= column_frame
	end
	return ret
end
