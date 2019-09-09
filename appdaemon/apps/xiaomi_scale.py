import appdaemon.plugins.hass.hassapi as hass
import appdaemon.appapi as appapi
import time
from datetime import datetime

import Xiaomi_Scale_Body_Metrics
# Adapted from lolouk44's Xiaomi Scale
# https://github.com/lolouk44/xiaomi_mi_scale

#add users here
users = ( 
    {
        "name": "User1",
        "height": 185,
        "age": "1901-01-01",
        "sex": "male",
        "weight_max" : 300,
        "weight_min" : 135,
        "notification_target" : "pushbullet_target_for_user1"
    }, 
    {
        "name": "User2",
        "height": 162,
        "age": "1901-01-01",
        "sex": "female",
        "weight_max" : 135,
        "weight_min" : 80,
        "notification_target" : "pushbullet_target_for_user2"
    })

create_stat_sensors = True

class xiaomiscale(hass.Hass):
	
	def initialize(self):
		self.log("xiaomi scale initialized")
		# Create a sensor in Home Assistant for the MQTT Topic provided by our ESP32 Bridge - we could read this directly via MQTT, but it may be useful to have it as a sensor in HASS
		self.call_service("mqtt/publish", topic='homeassistant/sensor/scale/config', payload='{"name": "scale", "state_topic": "stat/scale", "json_attributes_topic": "stat/scale/attributes"}', retain=True)
		# Listen for this Sensor
		self.listen_state(self.process_scale_data, "sensor.scale" )
		
	def GetAge(self, d1):
		d1 = datetime.strptime(d1, "%Y-%m-%d")
		return abs((datetime.now() - d1).days)/365

	def process_scale_data( self, attribute, entity, new_state, old_state, kwargs ):
		weight = float(self.get_state(entity="sensor.scale", attribute="Weight"))
		impedance = float(self.get_state(entity="sensor.scale", attribute="Impedance"))
		units = self.get_state(entity="sensor.scale", attribute="Units")
		timestamp = self.get_state(entity="sensor.scale", attribute="Timestamp")
		mitdatetime = datetime.strptime(timestamp, "%Y %m %d %H %M %S")
		self._publish( weight, units, mitdatetime, impedance )
	
	def get_diff( self, user, stat_name, stat_value ):
		prev_state = self.get_state( "sensor.bodymetrics_" + str(user["name"] ).lower() + "_" + stat_name )
		if prev_state is not None:
			delta = round( ( 1.0 - float( prev_state ) / stat_value ) * 100, 2 )
			diff = ' ▲' + str( delta ) + '%\n' if delta > 0 else ' ▼' + str( delta ) + '%\n'
			return diff
		#if we don't have data, just return a line-end for the message
		return '\n'
		
	def _publish(self, weight, unit, mitdatetime, miimpedance):
		# Check users for a match
		for user in users :
			if weight > float(user["weight_min"]) and weight < float(user["weight_max"]) :
				weightkg = weight
				if unit == 'lbs':
					weightkg = weightkg/2.205
				imp = 0
				if miimpedance:
					imp = int(miimpedance)
				
				lib = Xiaomi_Scale_Body_Metrics.bodyMetrics(weightkg, user["height"], self.GetAge(user["age"]), user["sex"], imp)
				
				disp_weight = 	round( weight, 						2 )
				bmi = 			round( lib.getBMI(), 				2 )
				bmr = 			round( lib.getBMR(), 				2 )
				vfat = 			round( lib.getVisceralFat(),		2 )
				lbm = 			round( lib.getLBMCoefficient(), 	2 )
				bfat = 			round( lib.getFatPercentage(),		2 )
				water = 		round( lib.getWaterPercentage(), 	2 )
				bone = 			round( lib.getBoneMass(),			2 )
				muscle = 		round( lib.getMuscleMass(), 		2 )
				protein = 		round( lib.getProteinPercentage(),	2 )
				
				message = '{'
				message += '"friendly_name":"' + user["name"] + '\'s Body Composition"'
				message += ',"Weight":' + str(disp_weight)
				message += ',"BMI":' + str(bmi)
				message += ',"Basal_Metabolism":' + str(bmr)
				message += ',"Visceral_Fat":' + str(vfat)

				if miimpedance:
					message += ',"Lean_Body_Mass":' + str(lbm)
					message += ',"Body_Fat":' + str(bfat)
					message += ',"Water":' + str(water)
					message += ',"Bone_Mass":' + str(bone)
					message += ',"Muscle_Mass":' + str(muscle)
					message += ',"Protein":' + str(protein)
					
				message += ',"icon":"mdi:run"'
				message += ',"TimeStamp":"' + str(mitdatetime) + '"'
				message += ',"unit_of_measurement":"' + unit + '"' 
				message += '}'
				
				#self.mqtt_publish('stat/bodymetrics/'+user["name"], message, qos=1, retain=True)
				self.set_state("sensor.bodymetrics_" + str(user["name"]).lower(), state = weight, attributes = eval(message) )
				
				
				self.log( "Set sensor.bodymetrics_" + str(user["name"]).lower() + " : " + message )  
				
				diff_weight = '\n'
				diff_bmi = '\n'
				diff_bmr = '\n'
				diff_vfat = '\n'
				diff_lbm = '\n'
				diff_bfat = '\n'
				diff_water = '\n'
				diff_bone = '\n'
				diff_muscle = '\n'
				diff_protein = '\n'
				
				if create_stat_sensors is True:
					diff_weight = self.get_diff( user, "weight", weight )
					diff_bmi = self.get_diff( user, "bmi", bmi )
					diff_bmr = self.get_diff( user, "bmr", bmr )
					diff_vfat = self.get_diff( user, "vfat", vfat ) 
					diff_lbm = self.get_diff( user, "lbm", lbm )
					diff_bfat = self.get_diff( user, "bfat", bfat )
					diff_water = self.get_diff( user, "water", water )
					diff_bone = self.get_diff( user, "bone", bone )
					diff_muscle = self.get_diff( user, "muscle", muscle )
					diff_protein = self.get_diff( user, "protein", protein )
					self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_weight", state = weight, attributes = { "friendly_name" : "Weight", "icon":"mdi:weight-pound", "unit_of_measurement":unit } )
					self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_bmi", state = bmi, attributes = { "friendly_name" : "Body Mass Index", "icon":"mdi:scale-bathroom", "unit_of_measurement":"BMI" } )
					self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_bmr", state = bmr, attributes = { "friendly_name" : "Basal Metabolic Rate", "icon":"mdi:timetable", "unit_of_measurement":"kcal" } )
					self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_vfat", state = vfat, attributes = { "friendly_name" : "Visceral Fat", "icon":"mdi:scale-bathroom", "unit_of_measurement":"%" } )
					if miimpedance:
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_lbm", state = lbm, attributes = { "friendly_name" : "Lean Body Mass", "icon":"mdi:scale-bathroom", "unit_of_measurement":"%" } )
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_bfat", state = bfat, attributes = { "friendly_name" : "Body Fat", "icon":"mdi:scale-bathroom", "unit_of_measurement":"%" } )
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_water", state = water, attributes = { "friendly_name" : "Water", "icon":"mdi:scale-bathroom", "unit_of_measurement":"%" } )
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_bone", state = bone, attributes = { "friendly_name" : "Bone Mass", "icon":"mdi:scale-bathroom", "unit_of_measurement":unit } )
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_muscle", state = muscle, attributes = { "friendly_name" : "Muscle Mass", "icon":"mdi:scale-bathroom", "unit_of_measurement":unit } )
						self.set_state("sensor.bodymetrics_" + str(user["name"]).lower() + "_protein", state = protein, attributes = { "friendly_name" : "Protein", "icon":"mdi:scale-bathroom", "unit_of_measurement":"%" } )
						
				if user["notification_target"]:
					msg_title = user["name"] + '\'s Body Composition'
					msg_body =  'Weight : ' + str(disp_weight) + 'lbs' + diff_weight
					msg_body += 'BMI : ' + str(bmi) + 'BMI' + diff_bmi
					msg_body += 'Basal Metabolism : ' + str(bmr) + 'kcal' + diff_bmr
					msg_body += 'Visceral Fat : ' + str(vfat) + '%' + diff_vfat
					if miimpedance:
						msg_body += 'Body Fat : ' + str(bfat) + '%' + diff_bfat
						msg_body += 'Lean Body Mass : ' + str(lbm) + '%' + diff_lbm
						msg_body += 'Muscle Mass : ' + str(muscle) + 'lbs' + diff_muscle
						msg_body += 'Bone Mass : ' + str(bone) + 'lbs' + diff_bone
						msg_body += 'Water : ' + str(water) + '%' + diff_water
						msg_body += 'Protein : ' + str(protein) + '%' + diff_protein
					self.call_service("notify/pushbullet", title=msg_title,message=msg_body, target=user["notification_target"])
					self.log( "Notification sent to " + str(user["name"]).lower() + " : " + msg_body )  
				break

