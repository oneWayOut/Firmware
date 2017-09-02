
#include "FixedwingPositionControl.hpp"

#define G_CONST 9.81f

//caitodo add battery_status

extern "C" __EXPORT int fw_pos_control_l1_main(int argc, char *argv[]);

FixedwingPositionControl *l1_control::g_control;
static int _control_task = -1;			///< task handle for sensor task */

FixedwingPositionControl::FixedwingPositionControl() :
	_loop_perf(perf_alloc(PC_ELAPSED, "fw l1 control"))
{
	_parameter_handles.l1_period = param_find("FW_L1_PERIOD");
	_parameter_handles.l1_damping = param_find("FW_L1_DAMPING");

	_parameter_handles.airspeed_min = param_find("FW_AIRSPD_MIN");
	_parameter_handles.airspeed_trim = param_find("FW_AIRSPD_TRIM");
	_parameter_handles.airspeed_max = param_find("FW_AIRSPD_MAX");
	_parameter_handles.airspeed_trans = param_find("VT_ARSP_TRANS");
	_parameter_handles.airspeed_mode = param_find("FW_ARSP_MODE");

	_parameter_handles.pitch_limit_min = param_find("FW_P_LIM_MIN");
	_parameter_handles.pitch_limit_max = param_find("FW_P_LIM_MAX");
	_parameter_handles.roll_limit = param_find("FW_R_LIM");
	_parameter_handles.throttle_min = param_find("FW_THR_MIN");
	_parameter_handles.throttle_max = param_find("FW_THR_MAX");
	_parameter_handles.throttle_idle = param_find("FW_THR_IDLE");
	_parameter_handles.throttle_slew_max = param_find("FW_THR_SLEW_MAX");
	_parameter_handles.throttle_cruise = param_find("FW_THR_CRUISE");
	_parameter_handles.throttle_land_max = param_find("FW_THR_LND_MAX");
	_parameter_handles.man_roll_max_deg = param_find("FW_MAN_R_MAX");
	_parameter_handles.man_pitch_max_deg = param_find("FW_MAN_P_MAX");
	_parameter_handles.rollsp_offset_deg = param_find("FW_RSP_OFF");
	_parameter_handles.pitchsp_offset_deg = param_find("FW_PSP_OFF");

	_parameter_handles.land_slope_angle = param_find("FW_LND_ANG");
	_parameter_handles.land_H1_virt = param_find("FW_LND_HVIRT");
	_parameter_handles.land_flare_alt_relative = param_find("FW_LND_FLALT");
	_parameter_handles.land_flare_pitch_min_deg = param_find("FW_LND_FL_PMIN");
	_parameter_handles.land_flare_pitch_max_deg = param_find("FW_LND_FL_PMAX");
	_parameter_handles.land_thrust_lim_alt_relative = param_find("FW_LND_TLALT");
	_parameter_handles.land_heading_hold_horizontal_distance = param_find("FW_LND_HHDIST");
	_parameter_handles.land_use_terrain_estimate = param_find("FW_LND_USETER");
	_parameter_handles.land_airspeed_scale = param_find("FW_LND_AIRSPD_SC");

	_parameter_handles.time_const = 			param_find("FW_T_TIME_CONST");
	_parameter_handles.time_const_throt = 			param_find("FW_T_THRO_CONST");
	_parameter_handles.min_sink_rate = 			param_find("FW_T_SINK_MIN");
	_parameter_handles.max_sink_rate =			param_find("FW_T_SINK_MAX");
	_parameter_handles.max_climb_rate =			param_find("FW_T_CLMB_MAX");
	_parameter_handles.climbout_diff =			param_find("FW_CLMBOUT_DIFF");
	_parameter_handles.throttle_damp = 			param_find("FW_T_THR_DAMP");
	_parameter_handles.integrator_gain =			param_find("FW_T_INTEG_GAIN");
	_parameter_handles.vertical_accel_limit =		param_find("FW_T_VERT_ACC");
	_parameter_handles.height_comp_filter_omega =		param_find("FW_T_HGT_OMEGA");
	_parameter_handles.speed_comp_filter_omega =		param_find("FW_T_SPD_OMEGA");
	_parameter_handles.roll_throttle_compensation = 	param_find("FW_T_RLL2THR");
	_parameter_handles.speed_weight = 			param_find("FW_T_SPDWEIGHT");
	_parameter_handles.pitch_damping = 			param_find("FW_T_PTCH_DAMP");
	_parameter_handles.heightrate_p =			param_find("FW_T_HRATE_P");
	_parameter_handles.heightrate_ff =			param_find("FW_T_HRATE_FF");
	_parameter_handles.speedrate_p =			param_find("FW_T_SRATE_P");

	_parameter_handles.vtol_type = 				param_find("VT_TYPE");

	/* fetch initial parameter values */
	parameters_update();
}

FixedwingPositionControl::~FixedwingPositionControl()
{
	if (_control_task != -1) {

		/* task wakes up every 100ms or so at the longest */
		_task_should_exit = true;

		/* wait for a second for the task to quit at our request */
		unsigned i = 0;

		do {
			/* wait 20ms */
			usleep(20000);

			/* if we have given up, kill it */
			if (++i > 50) {
				px4_task_delete(_control_task);
				break;
			}
		} while (_control_task != -1);
	}

	l1_control::g_control = nullptr;
}

int
FixedwingPositionControl::parameters_update()
{
	/* L1 control parameters */
	param_get(_parameter_handles.l1_damping, &(_parameters.l1_damping));
	param_get(_parameter_handles.l1_period, &(_parameters.l1_period));

	param_get(_parameter_handles.airspeed_min, &(_parameters.airspeed_min));
	param_get(_parameter_handles.airspeed_trim, &(_parameters.airspeed_trim));
	param_get(_parameter_handles.airspeed_max, &(_parameters.airspeed_max));
	param_get(_parameter_handles.airspeed_trans, &(_parameters.airspeed_trans));
	param_get(_parameter_handles.airspeed_mode, &(_parameters.airspeed_mode));

	param_get(_parameter_handles.pitch_limit_min, &(_parameters.pitch_limit_min));
	param_get(_parameter_handles.pitch_limit_max, &(_parameters.pitch_limit_max));
	param_get(_parameter_handles.roll_limit, &(_parameters.roll_limit));
	param_get(_parameter_handles.throttle_min, &(_parameters.throttle_min));
	param_get(_parameter_handles.throttle_max, &(_parameters.throttle_max));
	param_get(_parameter_handles.throttle_idle, &(_parameters.throttle_idle));
	param_get(_parameter_handles.throttle_cruise, &(_parameters.throttle_cruise));
	param_get(_parameter_handles.throttle_slew_max, &(_parameters.throttle_slew_max));

	param_get(_parameter_handles.throttle_land_max, &(_parameters.throttle_land_max));

	param_get(_parameter_handles.man_roll_max_deg, &_parameters.man_roll_max_rad);
	_parameters.man_roll_max_rad = radians(_parameters.man_roll_max_rad);
	param_get(_parameter_handles.man_pitch_max_deg, &_parameters.man_pitch_max_rad);
	_parameters.man_pitch_max_rad = radians(_parameters.man_pitch_max_rad);

	param_get(_parameter_handles.rollsp_offset_deg, &_parameters.rollsp_offset_rad);
	_parameters.rollsp_offset_rad = radians(_parameters.rollsp_offset_rad);
	param_get(_parameter_handles.pitchsp_offset_deg, &_parameters.pitchsp_offset_rad);
	_parameters.pitchsp_offset_rad = radians(_parameters.pitchsp_offset_rad);

	param_get(_parameter_handles.time_const, &(_parameters.time_const));
	param_get(_parameter_handles.time_const_throt, &(_parameters.time_const_throt));
	param_get(_parameter_handles.min_sink_rate, &(_parameters.min_sink_rate));
	param_get(_parameter_handles.max_sink_rate, &(_parameters.max_sink_rate));
	param_get(_parameter_handles.throttle_damp, &(_parameters.throttle_damp));
	param_get(_parameter_handles.integrator_gain, &(_parameters.integrator_gain));
	param_get(_parameter_handles.vertical_accel_limit, &(_parameters.vertical_accel_limit));
	param_get(_parameter_handles.height_comp_filter_omega, &(_parameters.height_comp_filter_omega));
	param_get(_parameter_handles.speed_comp_filter_omega, &(_parameters.speed_comp_filter_omega));
	param_get(_parameter_handles.roll_throttle_compensation, &(_parameters.roll_throttle_compensation));
	param_get(_parameter_handles.speed_weight, &(_parameters.speed_weight));
	param_get(_parameter_handles.pitch_damping, &(_parameters.pitch_damping));
	param_get(_parameter_handles.max_climb_rate, &(_parameters.max_climb_rate));
	param_get(_parameter_handles.climbout_diff, &(_parameters.climbout_diff));

	param_get(_parameter_handles.heightrate_p, &(_parameters.heightrate_p));
	param_get(_parameter_handles.heightrate_ff, &(_parameters.heightrate_ff));
	param_get(_parameter_handles.speedrate_p, &(_parameters.speedrate_p));

	param_get(_parameter_handles.land_slope_angle, &(_parameters.land_slope_angle));
	param_get(_parameter_handles.land_H1_virt, &(_parameters.land_H1_virt));
	param_get(_parameter_handles.land_flare_alt_relative, &(_parameters.land_flare_alt_relative));
	param_get(_parameter_handles.land_thrust_lim_alt_relative, &(_parameters.land_thrust_lim_alt_relative));

	/* check if negative value for 2/3 of flare altitude is set for throttle cut */
	if (_parameters.land_thrust_lim_alt_relative < 0.0f) {
		_parameters.land_thrust_lim_alt_relative = 0.66f * _parameters.land_flare_alt_relative;
	}

	param_get(_parameter_handles.land_heading_hold_horizontal_distance,
		  &(_parameters.land_heading_hold_horizontal_distance));
	param_get(_parameter_handles.land_flare_pitch_min_deg, &(_parameters.land_flare_pitch_min_deg));
	param_get(_parameter_handles.land_flare_pitch_max_deg, &(_parameters.land_flare_pitch_max_deg));
	param_get(_parameter_handles.land_use_terrain_estimate, &(_parameters.land_use_terrain_estimate));
	param_get(_parameter_handles.land_airspeed_scale, &(_parameters.land_airspeed_scale));
	param_get(_parameter_handles.vtol_type, &(_parameters.vtol_type));



	/* sanity check parameters */
	if (_parameters.airspeed_max < _parameters.airspeed_min ||
	    _parameters.airspeed_max < 5.0f ||
	    _parameters.airspeed_min > 100.0f ||
	    _parameters.airspeed_trim < _parameters.airspeed_min ||
	    _parameters.airspeed_trim > _parameters.airspeed_max) {

		PX4_WARN("error: airspeed parameters invalid");
		return PX4_ERROR;
	}



	/* Update and publish the navigation capabilities */
	_fw_pos_ctrl_status.landing_slope_angle_rad = _landingslope.landing_slope_angle_rad();
	_fw_pos_ctrl_status.landing_horizontal_slope_displacement = _landingslope.horizontal_slope_displacement();
	_fw_pos_ctrl_status.landing_flare_length = _landingslope.flare_length();
	fw_pos_ctrl_status_publish();


	return PX4_OK;
}

void
FixedwingPositionControl::vehicle_control_mode_poll()
{
	bool updated;

	orb_check(_control_mode_sub, &updated);

	if (updated) {
		orb_copy(ORB_ID(vehicle_control_mode), _control_mode_sub, &_control_mode);
	}
}



void
FixedwingPositionControl::vehicle_status_poll()
{
	bool updated;

	orb_check(_vehicle_status_sub, &updated);

	if (updated) {
		orb_copy(ORB_ID(vehicle_status), _vehicle_status_sub, &_vehicle_status);

		/* set correct uORB ID, depending on if vehicle is VTOL or not */
		if (_attitude_setpoint_id == nullptr) {
			if (_vehicle_status.is_vtol) {
				_attitude_setpoint_id = ORB_ID(fw_virtual_attitude_setpoint);

			} else {
				_attitude_setpoint_id = ORB_ID(vehicle_attitude_setpoint);
			}
		}
	}
}

void
FixedwingPositionControl::vehicle_land_detected_poll()
{
	bool updated;

	orb_check(_vehicle_land_detected_sub, &updated);

	if (updated) {
		orb_copy(ORB_ID(vehicle_land_detected), _vehicle_land_detected_sub, &_vehicle_land_detected);
	}
}

void
FixedwingPositionControl::manual_control_setpoint_poll()
{
	bool manual_updated;

	/* Check if manual setpoint has changed */
	orb_check(_manual_control_sub, &manual_updated);

	if (manual_updated) {
		orb_copy(ORB_ID(manual_control_setpoint), _manual_control_sub, &_manual);
	}
}

void
FixedwingPositionControl::control_state_poll()
{
	/* check if there is a new position */
	bool ctrl_state_updated;
	orb_check(_ctrl_state_sub, &ctrl_state_updated);

	if (ctrl_state_updated) {
		orb_copy(ORB_ID(control_state), _ctrl_state_sub, &_ctrl_state);
		_airspeed_valid = _ctrl_state.airspeed_valid;
		_airspeed_last_received = hrt_absolute_time();

	} else {

		/* no airspeed updates for one second */
		if (_airspeed_valid && (hrt_absolute_time() - _airspeed_last_received) > 1e6) {
			_airspeed_valid = false;
		}
	}

	/* set rotation matrix and euler angles */
	math::Quaternion q_att(_ctrl_state.q);
	_R_nb = q_att.to_dcm();

	math::Vector<3> euler_angles;
	euler_angles = _R_nb.to_euler();
	_roll    = euler_angles(0);
	_pitch   = euler_angles(1);
	_yaw     = euler_angles(2);
}

void
FixedwingPositionControl::position_setpoint_triplet_poll()
{
	/* check if there is a new setpoint */
	bool pos_sp_triplet_updated;
	orb_check(_pos_sp_triplet_sub, &pos_sp_triplet_updated);

	if (pos_sp_triplet_updated) {
		orb_copy(ORB_ID(position_setpoint_triplet), _pos_sp_triplet_sub, &_pos_sp_triplet);

		if (_pos_sp_triplet.current.type == position_setpoint_s::SETPOINT_TYPE_LAND)
		{
			//caitodo get _dH and _dL of landing waypoint
		}
		if (/* condition */)
		{
			//caitodo get trackangle of the route
		}

	}
}

void
FixedwingPositionControl::task_main_trampoline(int argc, char *argv[])
{
	l1_control::g_control = new FixedwingPositionControl();

	if (l1_control::g_control == nullptr) {
		PX4_WARN("OUT OF MEM");
		return;
	}

	/* only returns on exit */
	l1_control::g_control->task_main();
	delete l1_control::g_control;
	l1_control::g_control = nullptr;
}

void
FixedwingPositionControl::fw_pos_ctrl_status_publish()
{
	_fw_pos_ctrl_status.timestamp = hrt_absolute_time();

	if (_fw_pos_ctrl_status_pub != nullptr) {
		orb_publish(ORB_ID(fw_pos_ctrl_status), _fw_pos_ctrl_status_pub, &_fw_pos_ctrl_status);

	} else {
		_fw_pos_ctrl_status_pub = orb_advertise(ORB_ID(fw_pos_ctrl_status), &_fw_pos_ctrl_status);
	}
}

void
FixedwingPositionControl::get_waypoint_heading_distance(float heading, position_setpoint_s &waypoint_prev,
		position_setpoint_s &waypoint_next, bool flag_init)
{
	position_setpoint_s temp_prev = waypoint_prev;
	position_setpoint_s temp_next = waypoint_next;

	if (flag_init) {
		// previous waypoint: HDG_HOLD_SET_BACK_DIST meters behind us
		waypoint_from_heading_and_distance(_global_pos.lat, _global_pos.lon, heading + radians(180.0f),
						   HDG_HOLD_SET_BACK_DIST, &temp_prev.lat, &temp_prev.lon);

		// next waypoint: HDG_HOLD_DIST_NEXT meters in front of us
		waypoint_from_heading_and_distance(_global_pos.lat, _global_pos.lon, heading,
						   HDG_HOLD_DIST_NEXT, &temp_next.lat, &temp_next.lon);

	} else {
		// use the existing flight path from prev to next

		// previous waypoint: shifted HDG_HOLD_REACHED_DIST + HDG_HOLD_SET_BACK_DIST
		create_waypoint_from_line_and_dist(waypoint_next.lat, waypoint_next.lon, waypoint_prev.lat, waypoint_prev.lon,
						   HDG_HOLD_REACHED_DIST + HDG_HOLD_SET_BACK_DIST, &temp_prev.lat, &temp_prev.lon);

		// next waypoint: shifted -(HDG_HOLD_DIST_NEXT + HDG_HOLD_REACHED_DIST)
		create_waypoint_from_line_and_dist(waypoint_next.lat, waypoint_next.lon, waypoint_prev.lat, waypoint_prev.lon,
						   -(HDG_HOLD_REACHED_DIST + HDG_HOLD_DIST_NEXT), &temp_next.lat, &temp_next.lon);
	}

	waypoint_prev = temp_prev;
	waypoint_prev.alt = _hold_alt;
	waypoint_prev.valid = true;

	waypoint_next = temp_next;
	waypoint_next.alt = _hold_alt;
	waypoint_next.valid = true;
}


//caitodo checkthis
float
FixedwingPositionControl::get_terrain_altitude_takeoff(float takeoff_alt,
		const vehicle_global_position_s &global_pos)
{
	if (PX4_ISFINITE(global_pos.terrain_alt) && global_pos.terrain_alt_valid) {
		return global_pos.terrain_alt;
	}

	return takeoff_alt;
}



bool
FixedwingPositionControl::in_takeoff_situation()
{
	// in air for < 10s
	const hrt_abstime delta_takeoff = 10000000;

	return (hrt_elapsed_time(&_time_went_in_air) < delta_takeoff)
	       && (_global_pos.alt <= _takeoff_ground_alt + _parameters.climbout_diff);
}

void
FixedwingPositionControl::do_takeoff_help(float *hold_altitude, float *pitch_limit_min)
{
	/* demand "climbout_diff" m above ground if user switched into this mode during takeoff */
	if (in_takeoff_situation()) {
		*hold_altitude = _takeoff_ground_alt + _parameters.climbout_diff;
		*pitch_limit_min = radians(10.0f);

	} else {
		*pitch_limit_min = _parameters.pitch_limit_min;
	}
}

//caitodo update _scaler
void FixedwingPositionControl::filterHeight()
{
	//get filtered _height and _heightDot
	_height = ;
	_heightDot = ;
}

void FixedwingPositionControl::calcTrackError()
{
	_trackError    = ;
	_trackErrorVel = ;

}

void FixedwingPositionControl::control_thrust(float v_dmd, float dt)
{
	//demanded acceleration X;
	float _accX_dmd   =  _k_Vas*(_ctrl_state.airspeed - v_dmd);
	_accX_dmd = constrain(_accX_dmd, -2.0f, 2.0f);

	float i_term = _ctrl_state.x_acc - _accX_dmd - G_CONST*sinf(_pitch);

	i_term = constrain(i_term, -2.0f, 2.0f);

	_thrust_integ += dt * _k_i_AxP * i_term;
	_thrust_integ = constrain(-0.3f, 1.0f);

	float p_term = _k_AxP * (_ctrl_state.x_acc - G_CONST*sinf(_pitch));
	p_term       = constrain(p_term, -0.3f, 0.3f);

	_thrust_cmd = p_term + _thrust_integ;
}

/**
 * @param
 * @param
 * caitod  add mode param to this function
 */
void FixedwingPositionControl::control_pitch(float pitch_ref, float height_dot_dmd, float float dt)
{
	static float height_integ = 0.0f;

	float pitch_rate_dmd = 0.0f

	//calculate  pitch_rate_dmd in diffrent modes
	if(in_mission_mode)
	{
		height_integ += constrain(_heightDot - height_dot_dmd, -0.5f, 0.5f) * dt * _k_i_HdotE;
		height_integ = constrain(height_integ, radians(-5.0f), radians(5.0f));

		float pitch_dmd = height_integ + _k_HdotE * (_heightDot - height_dot_dmd) + pitch_ref;

		pitch_rate_dmd = _k_pitch_E * (_pitch - pitch_dmd) + (G_CONST/_ctrl_state.airspeed)*cosf(_pitch)*tanf(_roll)*sinf(_roll);
	}
	else if (in_takeoff_mode)
	{
		pitch_rate_dmd = _k_pitch_E * (_pitch - radians(15.0f));
		pitch_rate_dmd = constrain(pitch_rate_dmd, -radians(2.0f), radians(2.0f));
	}


	float pitch_rate_error = _ctrl_state.pitch_rate - pitch_rate_dmd;

	_pitch_integ += _k_i_QE * dt* constrain(pitch_rate_error, -radians(2.0f), radians(2.0f));
	_pitch_integ = constrain(_pitch_integ, -radians(15.0f), radians(15.0f));


	float p_term = _k_QE * pitch_rate_error;
	p_term = constrain(p_term, -radians(3.0f), radians(3.0f));

	_pitch_cmd = _scaler * (p_term + _pitch_integ + _k_QF * pitch_rate_dmd);
}


void FixedwingPositionControl::control_roll(int method, float dt)
{
	//caitodo get the yaw of the track
	float yaw_track = 0.0f;

	static float track_integ = 0.0f;
			
	if (method == 1)
	{
		_roll_cmd = _k_PA * _ctrl_state.roll_rate + _k_roll_A * _roll;
	}else{
		float trackErrorVError = _trackErrorVel - _k_YA *constrain(_trackError, -200.0f, 200.0f);

		track_integ += _k_i_YdotA * dt * constrain(trackErrorVError, -1.0f, 1.0f);
		track_integ = constrain(track_integ, -radians(2.0f), radians(2.0f));

		float roll_dmd = _k_YdotA * trackErrorVError + track_integ + _k_yaw_A * (_yaw-yaw_track);
		roll_dmd = constrain(roll_dmd, -radians(30.0f), radians(30.0f));

		float roll_rate_dmd = _k_roll_A * (_roll - roll_dmd) - (G_CONST/_ctrl_state.airspeed)*tanf(_roll)*sinf(_pitch);
		roll_rate_dmd  = constrain(roll_rate_dmd, -radians(5.0f), radians(5.0f));

		_roll_integ += _k_i_PA*dt * (_ctrl_state.roll_rate - roll_rate_dmd);
		_roll_integ = constrain(_roll_integ, -radians(10.0f), radians(10.0f));

		_roll_cmd = _scaler * _k_PA *(_ctrl_state.roll_rate - roll_rate_dmd) + _roll_integ + _k_PF*roll_rate_dmd;
	}
}


void FixedwingPositionControl::control_yaw(float dt)
{
	float yaw_rate_dmd = G_CONST/_ctrl_state.airspeed * tanf(_roll)*cosf(_roll)*cosf(_pitch);

	_yaw_integ += _k_i_RA*dt*constrain(_ctrl_state.yaw_rate - yaw_rate_dmd, -radians(1.0f), radians(1.0f));
	_yaw_integ = constrain(_yaw_integ, -radians(5.0f), radians(5.0f));

	_yaw_cmd = _scaler * _k_RA*(_ctrl_state.yaw_rate - yaw_rate_dmd) + _yaw_integ + _k_RF * yaw_rate_dmd;
}



bool
FixedwingPositionControl::control_position(const math::Vector<2> &curr_pos, const math::Vector<2> &ground_speed,
		const position_setpoint_s &pos_sp_prev, const position_setpoint_s &pos_sp_curr)
{
	float dt = 0.01f;

	if (_control_position_last_called > 0) {
		dt = hrt_elapsed_time(&_control_position_last_called) * 1e-6f;
	}

	_control_position_last_called = hrt_absolute_time();

	bool setpoint = true;

	_att_sp.fw_control_yaw = false;		// by default we don't want yaw to be contoller directly with rudder
	_att_sp.apply_flaps = false;		// by default we don't use flaps

	/* filter speed and altitude for controller */
	math::Vector<3> accel_body(_ctrl_state.x_acc, _ctrl_state.y_acc, _ctrl_state.z_acc);

	math::Vector<3> accel_earth{_R_nb * accel_body};

	//caitodo filter airspeed and height height speed!!! 


	// l1 navigation logic breaks down when wind speed exceeds max airspeed
	// compute 2D groundspeed from airspeed-heading projection
	math::Vector<2> air_speed_2d{_ctrl_state.airspeed * cosf(_yaw), _ctrl_state.airspeed * sinf(_yaw)};
	math::Vector<2> nav_speed_2d{0.0f, 0.0f};

	// angle between air_speed_2d and ground_speed
	float air_gnd_angle = acosf((air_speed_2d * ground_speed) / (air_speed_2d.length() * ground_speed.length()));

	// if angle > 90 degrees or groundspeed is less than threshold, replace groundspeed with airspeed projection
	if ((fabsf(air_gnd_angle) > M_PI_F) || (ground_speed.length() < 3.0f)) {
		nav_speed_2d = air_speed_2d;

	} else {
		nav_speed_2d = ground_speed;
	}

	/* no throttle limit as default */
	float throttle_max = 1.0f;

	/* save time when airplane is in air */
	if (!_was_in_air && !_vehicle_land_detected.landed) {
		_was_in_air = true;
		_time_went_in_air = hrt_absolute_time();
		_takeoff_ground_alt = _global_pos.alt;
	}

	/* reset flag when airplane landed */
	if (_vehicle_land_detected.landed) {
		_was_in_air = false;
	}



	if (_control_mode.flag_control_auto_enabled && pos_sp_curr.valid) {
		/* AUTONOMOUS FLIGHT */

		_control_mode_current = FW_POSCTRL_MODE_AUTO;

		/* reset hold altitude */
		_hold_alt = _global_pos.alt;

		/* reset hold yaw */
		_hdg_hold_yaw = _yaw;



		/* current waypoint (the one currently heading for) */
		math::Vector<2> curr_wp((float)pos_sp_curr.lat, (float)pos_sp_curr.lon);

		/* Initialize attitude controller integrator reset flags to 0 */
		_att_sp.roll_reset_integral = false;
		_att_sp.pitch_reset_integral = false;
		_att_sp.yaw_reset_integral = false;

		/* previous waypoint */
		math::Vector<2> prev_wp{0.0f, 0.0f};

		if (pos_sp_prev.valid) {
			prev_wp(0) = (float)pos_sp_prev.lat;
			prev_wp(1) = (float)pos_sp_prev.lon;

		} else {
			/*
			 * No valid previous waypoint, go for the current wp.
			 * This is automatically handled by the L1 library.
			 */
			prev_wp(0) = (float)pos_sp_curr.lat;
			prev_wp(1) = (float)pos_sp_curr.lon;
		}

		float mission_airspeed = _parameters.airspeed_trim;

		if (PX4_ISFINITE(pos_sp_curr.cruising_speed) &&
		    pos_sp_curr.cruising_speed > 0.1f) {

			mission_airspeed = pos_sp_curr.cruising_speed;
		}

		float mission_throttle = _parameters.throttle_cruise;

		if (PX4_ISFINITE(pos_sp_curr.cruising_throttle) &&
		    pos_sp_curr.cruising_throttle > 0.01f) {

			mission_throttle = pos_sp_curr.cruising_throttle;
		}


		float height_dot_dmd = 0.0f;
		switch(pos_sp_curr.type)
		{
		case position_setpoint_s::SETPOINT_TYPE_IDLE:
			_att_sp.thrust = 0.0f;
			_att_sp.roll_body = 0.0f;
			_att_sp.pitch_body = 0.0f;
			break;
		case position_setpoint_s::SETPOINT_TYPE_TAKEOFF:
			static int takeoff_state = -1;  //

			switch(takeoff_state)
			{
			case -1:   //takeoff uninitialized caitodo check armed state????
				takeoff_state = 0;
				_tkoff_wp(0) = (float)_global_pos.lat;
				_tkoff_wp(1) = (float)_global_pos.lon;
				_tkoff_alt   = _global_pos.alt;

				_thrust_integ = 0.5f;

				_tkoff_yaw = 0.0f  //caitodo calculate the yaw of the runway;
				break;
			case 0:  //on runway and armed
				_pitch_cmd = 0.0f;

				//calculate thrust cmd
				control_thrust(15.0f);

				control_roll(1, dt);

				//calculate wheel and yaw cmd

				float yaw_term = constrain(_k_yaw * (_yaw - _tkoff_yaw), radians(-5.0f), radians(5.0f));
				float track_term = constrain(_k_track * _trackError, radians(-3.0f), radians(3.0f));
				_wheel_cmd = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term+track_term);
				_yaw_cmd   = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term + _k_R*_ctrl_state.yaw_rate + track_term);

				if (_ctrl_state.airspeed > _k_Vmin) {
					takeoff_state = 1;
					//reset thrust integerator
					_thrust_integ = 0.5f;
					mavlink_log_info(mavlink_log_pub, "#cdcTakeoff airspeed reached");
				}
				break;
			case 1:  //reached airspeed,  front wheel liftoff
				control_thrust(15.0f);

				//calculate demanded pitch;
				control_pitch(dt);

				control_roll(1,dt);

				//calculate wheel and yaw cmd

				float yaw_term = constrain(_k_yaw * (_yaw - _tkoff_yaw), radians(-5.0f), radians(5.0f));
				float track_term = constrain(_k_track * _trackError, radians(-3.0f), radians(3.0f));
				_wheel_cmd = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term+track_term);
				_yaw_cmd   = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term + _k_R*_ctrl_state.yaw_rate + track_term);


				if (_global_pos.alt > _tkoff_alt+15.0f)
				{
					takeoff_state = 2;
					mavlink_log_info(mavlink_log_pub, "#cdcNavigating to waypoint");
				}
				break;
			case 2:

				//consider takeoff wp as a normal wp.
			//caitodo 
				break;
			}

			break;
		case position_setpoint_s::SETPOINT_TYPE_POSITION:
			//calculate demanded pitch rate
			height_dot_dmd = _k_HE * (_height - (_global_pos.alt-pos_sp_curr.alt));
			height_dot_dmd = cosntrain(height_dot_dmd, -3.0f, 3.0f);

			control_pitch(radians(2.0f), height_dot_dmd,  dt);

			if (_global_pos.alt > _tkoff_alt +15.0f)
			{
				control_roll(2, dt);
			}else {
				control_roll(1, dt);
			}

			control_thrust(15.0f, dt);

			control_yaw(dt);
			break;
		case position_setpoint_s::SETPOINT_TYPE_LAND:

			//calculate pitch control cmd
			if (_global_pos.alt > _tkoff_alt +15.0f)
			{
				//caitodo check if pos_sp_curr.alt or takeoff alt????
				height_dot_dmd = _k_HE *(_global_pos.alt - pos_sp_curr.alt) + ground_speed.length() *tanf(_dH/_dL);
				height_dot_dmd = constrain(height_dot_dmd, -3.0f, 3.0f);
				control_pitch(radians(-3.0f), height_dot_dmd, dt);
			} else{
				height_dot_dmd = ground_speed.length() *tanf(_dH/_dL) * expf(0.1f*(_global_pos.alt- _tkoff_alt-15.0f));
				height_dot_dmd = constrain(height_dot_dmd, -3.0f, 3.0f);
				control_pitch(0.0f, height_dot_dmd, dt);
			}

			//roll, yaw and thrust
			if (_global_pos.alt > _tkoff_alt +2.0f)
			{
				control_roll(2, dt);
				control_yaw(dt);
			} else{
				control_roll(1, dt);

				//caitodo get landing runway yaw!!!!
				float yaw_term = constrain(_k_yaw * (_yaw - _tkoff_yaw), radians(-5.0f), radians(5.0f));
				float track_term = constrain(_k_track * _trackError, radians(-3.0f), radians(3.0f));
				_wheel_cmd = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term+track_term);
				_yaw_cmd   = (_k_Vmin/(_ctrl_state.airspeed+_k_Vmin)) * (yaw_term + _k_R*_ctrl_state.yaw_rate + track_term);


				control_thrust(8.0f, dt);
				//caitodo stop motors if height below 2m for 5 seconds!!
			}
	
			break;
		default:
		break;
		}
	} 
	else {
		_control_mode_current = FW_POSCTRL_MODE_OTHER;

		/* do not publish the setpoint */
		setpoint = false;

		// reset hold altitude
		_hold_alt = _global_pos.alt;

		/* reset landing and takeoff state */
		if (!_last_manual) {
			reset_landing_state();
			reset_takeoff_state();
		}
	}

	
	if (_control_mode.flag_control_position_enabled) {
		_last_manual = false;

	} else {
		_last_manual = true;
	}

	return setpoint;
}




void
FixedwingPositionControl::task_main()
{
	/*
	 * do subscriptions
	 */
	_global_pos_sub = orb_subscribe(ORB_ID(vehicle_global_position));
	_pos_sp_triplet_sub = orb_subscribe(ORB_ID(position_setpoint_triplet));
	_ctrl_state_sub = orb_subscribe(ORB_ID(control_state));
	_control_mode_sub = orb_subscribe(ORB_ID(vehicle_control_mode));
	_vehicle_command_sub = orb_subscribe(ORB_ID(vehicle_command));
	_vehicle_status_sub = orb_subscribe(ORB_ID(vehicle_status));
	_vehicle_land_detected_sub = orb_subscribe(ORB_ID(vehicle_land_detected));
	_params_sub = orb_subscribe(ORB_ID(parameter_update));
	_manual_control_sub = orb_subscribe(ORB_ID(manual_control_setpoint));

	/* rate limit control mode updates to 5Hz */
	orb_set_interval(_control_mode_sub, 200);
	/* rate limit vehicle status updates to 5Hz */
	orb_set_interval(_vehicle_status_sub, 200);
	/* rate limit vehicle land detected updates to 5Hz */
	orb_set_interval(_vehicle_land_detected_sub, 200);
	/* rate limit position updates to 50 Hz */
	orb_set_interval(_global_pos_sub, 20);

	/* abort on a nonzero return value from the parameter init */
	if (parameters_update() != PX4_OK) {
		/* parameter setup went wrong, abort */
		PX4_WARN("aborting startup due to errors.");
		_task_should_exit = true;
	}

	/* wakeup source(s) */
	px4_pollfd_struct_t fds[2];

	/* Setup of loop */
	fds[0].fd = _params_sub;
	fds[0].events = POLLIN;
	fds[1].fd = _global_pos_sub;
	fds[1].events = POLLIN;

	_task_running = true;

	while (!_task_should_exit) {

		/* wait for up to 500ms for data */
		int pret = px4_poll(&fds[0], (sizeof(fds) / sizeof(fds[0])), 100);

		/* timed out - periodic check for _task_should_exit, etc. */
		if (pret == 0) {
			continue;
		}

		/* this is undesirable but not much we can do - might want to flag unhappy status */
		if (pret < 0) {
			warn("poll error %d, %d", pret, errno);
			continue;
		}

		vehicle_control_mode_poll();

		vehicle_land_detected_poll();
		vehicle_status_poll();

		/* only update parameters if they changed */
		if ((fds[0].revents & POLLIN) != 0) {
			/* read from param to clear updated flag */
			parameter_update_s update {};
			orb_copy(ORB_ID(parameter_update), _params_sub, &update);

			/* update parameters from storage */
			parameters_update();
		}

		/* only run controller if position changed */
		if ((fds[1].revents & POLLIN) != 0) {
			perf_begin(_loop_perf);

			/* load local copies */
			orb_copy(ORB_ID(vehicle_global_position), _global_pos_sub, &_global_pos);

			// handle estimator reset events. we only adjust setpoins for manual modes
			if (_control_mode.flag_control_manual_enabled) {
				if (_control_mode.flag_control_altitude_enabled && _global_pos.alt_reset_counter != _alt_reset_counter) {
					_hold_alt += _global_pos.delta_alt;
					// make TECS accept step in altitude and demanded altitude
					_tecs.handle_alt_step(_global_pos.delta_alt, _global_pos.alt);
				}
			}

			// update the reset counters in any case
			_alt_reset_counter = _global_pos.alt_reset_counter;
			_pos_reset_counter = _global_pos.lat_lon_reset_counter;

			control_state_poll();
			manual_control_setpoint_poll();
			position_setpoint_triplet_poll();

			math::Vector<2> curr_pos((float)_global_pos.lat, (float)_global_pos.lon);
			math::Vector<2> ground_speed(_global_pos.vel_n, _global_pos.vel_e);

			/*
			 * Attempt to control position, on success (= sensors present and not in manual mode),
			 * publish setpoint.
			 */
			if (control_position(curr_pos, ground_speed, _pos_sp_triplet.previous, _pos_sp_triplet.current)) {
				_att_sp.timestamp = hrt_absolute_time();

				// add attitude setpoint offsets
				_att_sp.roll_body += _parameters.rollsp_offset_rad;
				_att_sp.pitch_body += _parameters.pitchsp_offset_rad;

				if (_control_mode.flag_control_manual_enabled) {
					_att_sp.roll_body = constrain(_att_sp.roll_body, -_parameters.man_roll_max_rad, _parameters.man_roll_max_rad);
					_att_sp.pitch_body = constrain(_att_sp.pitch_body, -_parameters.man_pitch_max_rad, _parameters.man_pitch_max_rad);
				}

				Quatf q(Eulerf(_att_sp.roll_body, _att_sp.pitch_body, _att_sp.yaw_body));
				q.copyTo(_att_sp.q_d);
				_att_sp.q_d_valid = true;

				if (!_control_mode.flag_control_offboard_enabled ||
				    _control_mode.flag_control_position_enabled ||
				    _control_mode.flag_control_velocity_enabled ||
				    _control_mode.flag_control_acceleration_enabled) {

					/* lazily publish the setpoint only once available */
					if (_attitude_sp_pub != nullptr) {
						/* publish the attitude setpoint */
						orb_publish(_attitude_setpoint_id, _attitude_sp_pub, &_att_sp);

					} else if (_attitude_setpoint_id != nullptr) {
						/* advertise and publish */
						_attitude_sp_pub = orb_advertise(_attitude_setpoint_id, &_att_sp);
					}
				}

				/* XXX check if radius makes sense here */
				float turn_distance = _l1_control.switch_distance(100.0f);

				/* lazily publish navigation capabilities */
				if ((hrt_elapsed_time(&_fw_pos_ctrl_status.timestamp) > 1000000)
				    || (fabsf(turn_distance - _fw_pos_ctrl_status.turn_distance) > FLT_EPSILON
					&& turn_distance > 0)) {

					/* set new turn distance */
					_fw_pos_ctrl_status.turn_distance = turn_distance;

					_fw_pos_ctrl_status.nav_roll = _l1_control.nav_roll();
					_fw_pos_ctrl_status.nav_pitch = get_tecs_pitch();
					_fw_pos_ctrl_status.nav_bearing = _l1_control.nav_bearing();

					_fw_pos_ctrl_status.target_bearing = _l1_control.target_bearing();
					_fw_pos_ctrl_status.xtrack_error = _l1_control.crosstrack_error();

					math::Vector<2> curr_wp((float)_pos_sp_triplet.current.lat, (float)_pos_sp_triplet.current.lon);

					_fw_pos_ctrl_status.wp_dist = get_distance_to_next_waypoint(curr_pos(0), curr_pos(1), curr_wp(0), curr_wp(1));

					fw_pos_ctrl_status_publish();
				}
			}

			perf_end(_loop_perf);
		}
	}

	_task_running = false;

	PX4_WARN("exiting.\n");

	_control_task = -1;
}

void
FixedwingPositionControl::reset_takeoff_state()
{
	// only reset takeoff if !armed or just landed
	if (!_control_mode.flag_armed || (_was_in_air && _vehicle_land_detected.landed)) {

		_runway_takeoff.reset();

		_launchDetector.reset();
		_launch_detection_state = LAUNCHDETECTION_RES_NONE;
		_launch_detection_notify = 0;

	} else {
		_launch_detection_state = LAUNCHDETECTION_RES_DETECTED_ENABLEMOTORS;
	}
}

void
FixedwingPositionControl::reset_landing_state()
{
	_time_started_landing = 0;

	// reset terrain estimation relevant values
	_time_last_t_alt = 0;

	_land_noreturn_horizontal = false;
	_land_noreturn_vertical = false;
	_land_stayonground = false;
	_land_motor_lim = false;
	_land_onslope = false;

	// reset abort land, unless loitering after an abort
	if (_fw_pos_ctrl_status.abort_landing
	    && _pos_sp_triplet.current.type != position_setpoint_s::SETPOINT_TYPE_LOITER) {

		_fw_pos_ctrl_status.abort_landing = false;
	}
}



int
FixedwingPositionControl::start()
{
	ASSERT(_control_task == -1);

	/* start the task */
	_control_task = px4_task_spawn_cmd("fw_pos_ctrl_l1",
					   SCHED_DEFAULT,
					   SCHED_PRIORITY_MAX - 5,
					   1700,
					   (px4_main_t)&FixedwingPositionControl::task_main_trampoline,
					   nullptr);

	if (_control_task < 0) {
		warn("task start failed");
		return -errno;
	}

	return PX4_OK;
}

int fw_pos_control_l1_main(int argc, char *argv[])
{
	if (argc < 2) {
		PX4_WARN("usage: fw_pos_control_l1 {start|stop|status}");
		return 1;
	}

	if (strcmp(argv[1], "start") == 0) {

		if (l1_control::g_control != nullptr) {
			PX4_WARN("already running");
			return 1;
		}

		if (OK != FixedwingPositionControl::start()) {
			warn("start failed");
			return 1;
		}

		/* avoid memory fragmentation by not exiting start handler until the task has fully started */
		while (l1_control::g_control == nullptr || !l1_control::g_control->task_running()) {
			usleep(50000);
			printf(".");
			fflush(stdout);
		}

		printf("\n");

		return 0;
	}

	if (strcmp(argv[1], "stop") == 0) {
		if (l1_control::g_control == nullptr) {
			PX4_WARN("not running");
			return 1;
		}

		delete l1_control::g_control;
		l1_control::g_control = nullptr;
		return 0;
	}

	if (strcmp(argv[1], "status") == 0) {
		if (l1_control::g_control != nullptr) {
			PX4_INFO("running");
			return 0;
		}

		PX4_WARN("not running");
		return 1;
	}

	PX4_WARN("unrecognized command");
	return 1;
}
