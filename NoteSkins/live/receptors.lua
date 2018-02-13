return function(button_list, stepstype, skin_parameters)
	local ret= {}
	local rots= {}
	local tap_redir= {
		Fret1= "White", Fret2= "White", Fret3= "White", Fret4= "Black", 
		-- Strum receptor is hidden, so it doesn't matter
		Fret5= "Black", Fret6= "Black", StrumDown= "Strum"
	}
	local x_reposition= {
		Fret1= 18, Fret2= 54, Fret3= 90, Fret4= -90, 
		Fret5= -54, Fret6= -18, StrumDown= -108
	}
	for i, button in ipairs(button_list) do
		ret[i]= Def.ActorFrame{
			Def.Sprite{
			Texture= tap_redir[button].." Ready Receptor.png", InitCommand= function(self)
				self:rotationz(0):effectclock("beat")
					:draworder(newfield_draw_order.receptor)
				if tap_redir[button] == "Strum" then
					self:visible(false)
				end
			end,
			WidthSetCommand= function(self, param)
				param.column:set_layer_fade_type(self, "FieldLayerFadeType_Receptor")
			end,
			},
			Def.Sprite{
			Texture= tap_redir[button].." Go Receptor.png", InitCommand= function(self)
				self:rotationz(0):effectclock("beat")
					:draworder(newfield_draw_order.receptor):visible(false)
			end,
			WidthSetCommand= function(self, param)
				param.column:set_layer_fade_type(self, "FieldLayerFadeType_Receptor")
			end,
			BeatUpdateCommand= function(self, param)
				-- Strum should never be visible
				if param.pressed and tap_redir[button] ~= "Strum" then
					self:visible(true)
				end
				-- Yo Kyzentun, you forgot to mention this nifty param
				if param.lifted then
					self:visible(false)
				end
			end,
			}
			}
	end
	return ret
end
