#!/usr/bin/python

#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
import os
import sys
import subprocess
import shlex
import time

class Test:
	def __init__(self, name, description, lrmd_location, test_tool_location, verbose = 0):
		self.name = name
		self.description = description
		self.cmds = []
		self.iteration = 0;
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location
		self.verbose = verbose

		self.result_txt = ""
		self.result_exitcode = 0;

		self.lrmd_process = None
		self.stonith_process = None

		self.executed = 0

	def __new_cmd(self, cmd, args, exitcode, stdout_match, no_wait = 0):
		if self.verbose and cmd == self.test_tool_location:
			args = args + " -V "

		self.cmds.append(
			{
				"cmd" : cmd,
				"args" : args,
				"expected_exitcode" : exitcode,
				"stdout_match" : stdout_match,
				"no_wait" : no_wait
			}
		)

	def start_environment(self):
		### make sure nothing we are still in control here ###
		cmd = shlex.split("killall -q -9 stonithd lrmd lt-lrmd lrmd_test lt-lrmd_test")
		test = subprocess.Popen(cmd, stdout=subprocess.PIPE)
		test.wait()

		self.stonith_process = subprocess.Popen(shlex.split("/usr/libexec/pacemaker/stonithd -s"))
		self.lrmd_process = subprocess.Popen(self.daemon_location)

		time.sleep(0.5)

	def clean_environment(self):
		if self.lrmd_process:
			self.lrmd_process.kill()
		if self.stonith_process:
			self.stonith_process.kill()

		self.lrmd_process = None
		self.stonith_process = None

	def add_sys_cmd(self, cmd, args):
		self.__new_cmd(cmd, args, 0, "")

	def add_sys_cmd_no_wait(self, cmd, args):
		self.__new_cmd(cmd, args, 0, "", 1)

	def add_cmd_check_stdout(self, args, stdout_match):
		self.__new_cmd(self.test_tool_location, args, 0, stdout_match)

	def add_cmd(self, args):
		self.__new_cmd(self.test_tool_location, args, 0, "")

	def add_expected_fail_cmd(self, args):
		self.__new_cmd(self.test_tool_location, args, 255, "")

	def get_exitcode(self):
		return self.result_exitcode

	def print_result(self, filler):
		print "%s%s" % (filler, self.result_txt)

	def run_cmd(self, args):
		cmd = shlex.split(args['args'])
		cmd.insert(0, args['cmd'])
		test = subprocess.Popen(cmd, stdout=subprocess.PIPE)

		if args['no_wait'] == 0:
			test.wait()
		else:
			return 0

		output = test.communicate()[0]

		if args['stdout_match'] != "" and output.count(args['stdout_match']) == 0:
			test.returncode = -2
			print "STDOUT string '%s' was not found in cmd output" % (args['stdout_match'])

		if self.verbose:
			print "%s" % output

		return test.returncode;

	def run(self):
		res = 0
		i = 1
		self.start_environment()

		if self.verbose:
			print "\n--- START TEST - %s" % self.name

		self.result_txt = "SUCCESS - '%s'" % (self.name)
		self.result_exitcode = 0
		for cmd in self.cmds:
			res = self.run_cmd(cmd)
			if res != cmd['expected_exitcode']:
				if self.verbose:
					print "Iteration %d FAILED - pid rc %d expected rc %d - cmd args '%s'" % (i, res, cmd['expected_exitcode'], cmd['args'])
				self.result_txt = "FAILURE - '%s' failed on cmd iteration %d" % (self.name, i)
				self.result_exitcode = -1
				break
			else:
				if self.verbose:
					print "Iteration %d SUCCESS" % (i)
			i = i + 1
		self.clean_environment()
		print self.result_txt
		if self.verbose:
			print "--- END TEST - %s\n" % self.name

		self.executed = 1
		return res

class Tests:
	def __init__(self, lrmd_location, test_tool_location, verbose = 0):
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location
		self.tests = []
		self.verbose = verbose

	def new_test(self, name, description):
		test = Test(name, description, self.daemon_location, self.test_tool_location, self.verbose)
		self.tests.append(test)
		return test

	def setup_test_environment(self):
		self.cleanup_test_environment()

		### Make fake systemd daemon and unit file ###
		dummy_daemon = "#!/bin/bash\nwhile true\ndo\nsleep 5\ndone"
		dummy_service_file = ("[Unit]\n"
			"Description=Dummy Resource\n"
			"[Service]\n"
			"Type=simple\n"
			"ExecStart=/usr/sbin/lrmd_dummy_daemon\n")
		os.system("cat <<-END >>/usr/sbin/lrmd_dummy_daemon\n%s\nEND" % (dummy_daemon))
		os.system("cat <<-END >>/lib/systemd/system/lrmd_dummy_daemon.service\n%s\nEND" % (dummy_service_file))
		os.system("chmod u+x /usr/sbin/lrmd_dummy_daemon")
		os.system("systemctl daemon-reload")

	def cleanup_test_environment(self):
		os.system("rm -f /lib/systemd/system/lrmd_dummy_daemon.service")
		os.system("rm -f /usr/sbin/lrmd_dummy_daemon")
		os.system("systemctl daemon-reload")

	### These are tests that should apply to all resource classes ###
	def build_generic_tests(self):

		rsc_classes = ["ocf", "lsb", "stonith", "systemd", "service"]
		common_cmds = {
			"ocf_reg_line"      : "-c register_rsc -r test_rsc -t 1000 -C ocf -P pacemaker -T Dummy",
			"ocf_reg_event"     : "-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"ocf_unreg_line"    : "-c unregister_rsc -r \"test_rsc\" -t 1000",
			"ocf_unreg_event"   : "-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"ocf_start_line"    : "-c exec -r \"test_rsc\" -a \"start\" -t 1000 ",
			"ocf_start_event"   : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"ocf_stop_line"     : "-c exec -r \"test_rsc\" -a \"stop\" -t 1000 ",
			"ocf_stop_event"    : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"ocf_monitor_line"  : "-c exec -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t 1000",
			"ocf_monitor_event" : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000",
			"ocf_cancel_line"   : "-c cancel -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t \"1000\" ",
			"ocf_cancel_event"  : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_CANCELLED\" ",

			"systemd_reg_line"      : "-c register_rsc -r test_rsc -t 1000 -C systemd -T lrmd_dummy_daemon",
			"systemd_reg_event"     : "-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"systemd_unreg_line"    : "-c unregister_rsc -r \"test_rsc\" -t 1000",
			"systemd_unreg_event"   : "-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"systemd_start_line"    : "-c exec -r \"test_rsc\" -a \"start\" -t 1000 ",
			"systemd_start_event"   : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"systemd_stop_line"     : "-c exec -r \"test_rsc\" -a \"stop\" -t 1000 ",
			"systemd_stop_event"    : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"systemd_monitor_line"  : "-c exec -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t 1000",
			"systemd_monitor_event" : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000",
			"systemd_cancel_line"   : "-c cancel -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t \"1000\" ",
			"systemd_cancel_event"  : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_CANCELLED\" ",

			"service_reg_line"      : "-c register_rsc -r test_rsc -t 1000 -C service -T lrmd_dummy_daemon",
			"service_reg_event"     : "-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"service_unreg_line"    : "-c unregister_rsc -r \"test_rsc\" -t 1000",
			"service_unreg_event"   : "-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"service_start_line"    : "-c exec -r \"test_rsc\" -a \"start\" -t 1000 ",
			"service_start_event"   : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"service_stop_line"     : "-c exec -r \"test_rsc\" -a \"stop\" -t 1000 ",
			"service_stop_event"    : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"service_monitor_line"  : "-c exec -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t 1000",
			"service_monitor_event" : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000",
			"service_cancel_line"   : "-c cancel -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t \"1000\" ",
			"service_cancel_event"  : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_CANCELLED\" ",

			"lsb_reg_line"      : "-c register_rsc -r test_rsc -t 1000 -C lsb -T \"/usr/share/pacemaker/tests/cts/LSBDummy\"",
			"lsb_reg_event"     : "-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"lsb_unreg_line"    : "-c unregister_rsc -r \"test_rsc\" -t 1000",
			"lsb_unreg_event"   : "-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"lsb_start_line"    : "-c exec -r \"test_rsc\" -a \"start\" -t 1000 ",
			"lsb_start_event"   : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"lsb_stop_line"     : "-c exec -r \"test_rsc\" -a \"stop\" -t 1000 ",
			"lsb_stop_event"    : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"lsb_monitor_line"  : "-c exec -r \"test_rsc\" -a status -i \"1000\" -t 1000",
			"lsb_monitor_event" : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:status rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000",
			"lsb_cancel_line"   : "-c cancel -r \"test_rsc\" -a \"status\" -i \"1000\" -t \"1000\" ",
			"lsb_cancel_event"  : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:status rc:0 exec_rc:0 op_status:OP_CANCELLED\" ",

			"stonith_reg_line"      : "-c register_rsc -r test_rsc -t 1000 -C stonith -P pacemaker -T fence_pcmk",
			"stonith_reg_event"     : "-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"stonith_unreg_line"    : "-c unregister_rsc -r \"test_rsc\" -t 1000",
			"stonith_unreg_event"   : "-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\"",
			"stonith_start_line"    : "-c exec -r \"test_rsc\" -a \"start\" -t 1000 ",
			"stonith_start_event"   : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"stonith_stop_line"     : "-c exec -r \"test_rsc\" -a \"stop\" -t 1000 ",
			"stonith_stop_event"    : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ",
			"stonith_monitor_line"  : "-c exec -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t 1000",
			"stonith_monitor_event" : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 3000",
			"stonith_cancel_line"   : "-c cancel -r \"test_rsc\" -a \"monitor\" -i \"1000\" -t \"1000\" ",
			"stonith_cancel_event"  : "-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_CANCELLED\" ",
		}

		### register/unregister tests ###
		for rsc in rsc_classes:
			test = self.new_test("generic_registration_%s" % (rsc), "Simple resource registration test for %s standard" % (rsc))
			test.add_cmd(common_cmds["%s_reg_line" % (rsc)] + " " + common_cmds["%s_reg_event" % (rsc)])
			test.add_cmd(common_cmds["%s_unreg_line" % (rsc)] + " " + common_cmds["%s_unreg_event" % (rsc)])

		### start/stop tests  ###
		for rsc in rsc_classes:
			test = self.new_test("generic_start_stop_%s" % (rsc), "Simple start and stop test for %s standard" % (rsc))
			test.add_cmd(common_cmds["%s_reg_line" % (rsc)]   + " " + common_cmds["%s_reg_event" % (rsc)])
			test.add_cmd(common_cmds["%s_start_line" % (rsc)] + " " + common_cmds["%s_start_event" % (rsc)])
			test.add_cmd(common_cmds["%s_stop_line" % (rsc)]  + " " + common_cmds["%s_stop_event" % (rsc)])
			test.add_cmd(common_cmds["%s_unreg_line" % (rsc)] + " " + common_cmds["%s_unreg_event" % (rsc)])

		### monitor test ###
		for rsc in rsc_classes:
			test = self.new_test("generic_monitor_%s" % (rsc), "Register a test, start, monitor a few times, then stop for %s standard" % (rsc))
			test.add_cmd(common_cmds["%s_reg_line" % (rsc)]   + " " + common_cmds["%s_reg_event" % (rsc)])
			test.add_cmd(common_cmds["%s_start_line" % (rsc)] + " " + common_cmds["%s_start_event" % (rsc)])
			test.add_cmd(common_cmds["%s_monitor_line" % (rsc)] + " " + common_cmds["%s_monitor_event" % (rsc)])
			test.add_cmd(common_cmds["%s_monitor_event" % (rsc)]) ### If this fails, that means the monitor is not being rescheduled ####
			test.add_cmd(common_cmds["%s_stop_line" % (rsc)]  + " " + common_cmds["%s_stop_event" % (rsc)])
			test.add_cmd(common_cmds["%s_unreg_line" % (rsc)] + " " + common_cmds["%s_unreg_event" % (rsc)])

		### monitor cancel test ###
		for rsc in rsc_classes:
			test = self.new_test("generic_monitor_cancel_%s" % (rsc), "Register a test, start, monitor a few times, then stop for %s standard" % (rsc))
			test.add_cmd(common_cmds["%s_reg_line" % (rsc)]   + " " + common_cmds["%s_reg_event" % (rsc)])
			test.add_cmd(common_cmds["%s_start_line" % (rsc)] + " " + common_cmds["%s_start_event" % (rsc)])
			test.add_cmd(common_cmds["%s_monitor_line" % (rsc)] + " " + common_cmds["%s_monitor_event" % (rsc)])
			test.add_cmd(common_cmds["%s_monitor_event" % (rsc)]) ### If this fails, that means the monitor may not be getting rescheduled ####
			test.add_cmd(common_cmds["%s_monitor_event" % (rsc)]) ### If this fails, that means the monitor may not be getting rescheduled ####
			test.add_cmd(common_cmds["%s_cancel_line" % (rsc)] + " " + common_cmds["%s_cancel_event" % (rsc)])
			test.add_expected_fail_cmd(common_cmds["%s_monitor_event" % (rsc)]) ### If this happens the monitor did not actually cancel correctly. ###
			test.add_expected_fail_cmd(common_cmds["%s_monitor_event" % (rsc)]) ### If this happens the monitor did not actually cancel correctly. ###
			test.add_cmd(common_cmds["%s_stop_line" % (rsc)]  + " " + common_cmds["%s_stop_event" % (rsc)])
			test.add_cmd(common_cmds["%s_unreg_line" % (rsc)] + " " + common_cmds["%s_unreg_event" % (rsc)])

	### These are tests that target specific cases ###
	def build_custom_tests(self):

		### start timeout test  ###
		test = self.new_test("start_timeout", "Register a test, then start with a 1ms timeout period.")
		test.add_cmd("-c register_rsc -r \"test_rsc\" -C \"ocf\" -P \"pacemaker\" -T \"Dummy\" -t 1000 "
			"-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"start\" -k \"op_sleep\" -v \"3\" -t 1000 -w")
		test.add_cmd("-l "
			"\"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:198 op_status:OP_TIMEOUT\" -t 3000")
		test.add_cmd("-c exec -r test_rsc -a stop -t 1000"
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c unregister_rsc -r test_rsc -t 1000 "
			"-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")

		### start delay /stop test ###
		test = self.new_test("start_delay_stop", "Register a test, then start with start_delay value, and stop it")
		test.add_cmd("-c register_rsc -r test_rsc -P pacemaker -C ocf -T Dummy "
			"-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c exec -r test_rsc -s 2000 -a start -w -t 1000")
		test.add_expected_fail_cmd("-l "
			"\"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" -t 1000")
		test.add_cmd("-l "
			"\"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" -t 3000")
		test.add_cmd("-c exec -r test_rsc -a stop -t 1000"
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:stop rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c unregister_rsc -r test_rsc -t 1000 "
			"-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")

		### monitor fail ###
		test = self.new_test("monitor_fail_ocf", "Register a test, then start, monitor a few times, then make the monitor fail.")
		test.add_cmd("-c register_rsc -r \"test_rsc\" -C \"ocf\" -P \"pacemaker\" -T \"Dummy\" -t 1000 "
			"-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"start\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"start\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"monitor\" -i \"100\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000")
		test.add_sys_cmd("rm", "-f /var/run/Dummy-test_rsc.state")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:7 op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel -r \"test_rsc\" -a \"monitor\" -i \"100\" -t \"1000\" "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:7 op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:7 op_status:OP_DONE\" -t 1000")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")


		test = self.new_test("monitor_fail_systemd", "Register a test, then start, monitor a few times, then make the monitor fail.")
		test.add_cmd("-c register_rsc -r \"test_rsc\" -C systemd -T lrmd_dummy_daemon -t 1000 "
			"-l \"NEW_EVENT event_type:register rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"start\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"start\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:start rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-c exec -r \"test_rsc\" -a \"monitor\" -i \"100\" -t 1000 "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" ")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 2000")
		test.add_sys_cmd("killall", "-q -9 lrmd_dummy_daemon")
		test.add_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:3 op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel -r \"test_rsc\" -a \"monitor\" -i \"100\" -t \"1000\" "
			"-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:3 op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:7 op_status:OP_DONE\" -t 1000")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:exec_complete rsc_id:test_rsc action:monitor rc:0 exec_rc:0 op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c unregister_rsc -r \"test_rsc\" -t 1000 "
			"-l \"NEW_EVENT event_type:unregister rsc_id:test_rsc action:none rc:0 exec_rc:0 op_status:OP_DONE\" ")

		### get metadata ###
		test = self.new_test("get_metadata", "Retrieve metadata for a resource")
		test.add_cmd_check_stdout("-c metadata -C \"ocf\" -P \"pacemaker\" -T \"Dummy\""
			,"resource-agent name=\"Dummy\"")
		test.add_cmd("-c metadata -C \"ocf\" -P \"pacemaker\" -T \"Stateful\"")
		test.add_expected_fail_cmd("-c metadata -P \"pacemaker\" -T \"Stateful\"")
		test.add_expected_fail_cmd("-c metadata -C \"ocf\" -P \"pacemaker\" -T \"fake_agent\"")

		### get stonith metadata ###
		test = self.new_test("get_stonith_metadata", "Retrieve stonith metadata for a resource")
		test.add_cmd_check_stdout("-c metadata -C \"stonith\" -P \"pacemaker\" -T \"fence_pcmk\"",
			"resource-agent name=\"fence_pcmk\"")

		### get agents ###
		test = self.new_test("check_agents", "Retrieve list of available resource agents, verifies at least one of each type exists.")
		test.add_cmd_check_stdout("-c list_agents ", "Dummy")       ### ocf ###
		test.add_cmd_check_stdout("-c list_agents -C ocf", "Dummy") ### ocf ###

		test.add_sys_cmd("cp", "/usr/share/pacemaker/tests/cts/LSBDummy /etc/init.d/LSBDummy")
		test.add_cmd_check_stdout("-c list_agents ", "LSBDummy")       ### init.d ###
		test.add_cmd_check_stdout("-c list_agents -C lsb", "LSBDummy") ### init.d ###
		test.add_sys_cmd("rm", "-f /etc/init.d/LSBDummy")

		test.add_cmd_check_stdout("-c list_agents ", "rsyslog")           ### systemd ###
		test.add_cmd_check_stdout("-c list_agents -C systemd", "rsyslog") ### systemd ###

		test.add_cmd_check_stdout("-c list_agents -C stonith", "fence_pcmk") ### stonith ###
		test.add_cmd_check_stdout("-c list_agents ", "fence_pcmk")           ### stonith ###

		### get ocf providers  ###
		test = self.new_test("list_ocf_providers", "Retrieve list of available resource providers, verifies pacemaker is a provider.")
		test.add_cmd_check_stdout("-c list_ocf_providers ", "pacemaker")
		test.add_cmd_check_stdout("-c list_ocf_providers -T ping", "pacemaker")

	def print_list(self):
		print "\n==== %d TESTS FOUND ====" % (len(self.tests))
		for test in self.tests:
			print "%s             - %s" % (test.name, test.description)
		print "==== END OF LIST ====\n"

	def run_single(self, name):
		for test in self.tests:
			if test.name == name:
				test.run()
				break;

	def run_tests_matching(self, pattern):
		for test in self.tests:
			if test.name.count(pattern) != 0:
				test.run()

	def run_tests(self):
		for test in self.tests:
			test.run()

	def print_results(self):
		failures = 0;
		success = 0;
		print "\n\n======= FINAL RESULTS =========="
		print "\n--- FAILURE RESULTS:"
		for test in self.tests:
			if test.executed == 0:
				continue

			if test.get_exitcode() != 0:
				failures = failures + 1
				test.print_result("    ")
			else:
				success = success + 1

		if failures == 0:
			print "    None"

		print "\n--- TOTALS\n    Pass:%d\n    Fail:%d\n" % (success, failures)

class TestOptions:
	def __init__(self):
		self.options = {}
		self.options['list-tests'] = 0
		self.options['run-all'] = 1
		self.options['run-only'] = ""
		self.options['run-only-pattern'] = ""
		self.options['verbose'] = 0
		self.options['invalid-arg'] = ""
		self.options['show-usage'] = 0

	def build_options(self, argv):
		args = argv[1:]
		skip = 0
		for i in range(0, len(args)):
			if skip:
				skip = 0
				continue
			elif args[i] == "-h" or args[i] == "--help":
				self.options['show-usage'] = 1
			elif args[i] == "-l" or args[i] == "--list-tests":
				self.options['list-tests'] = 1
			elif args[i] == "-V" or args[i] == "--verbose":
				self.options['verbose'] = 1
			elif args[i] == "-r" or args[i] == "--run-only":
				self.options['run-only'] = args[i+1]
				skip = 1
			elif args[i] == "-p" or args[i] == "--run-only-pattern":
				self.options['run-only-pattern'] = args[i+1]
				skip = 1

	def show_usage(self):
		print "usage: " + sys.argv[0] + " [options]"
		print "If no options are provided, all tests will run"
		print "Options:"
		print "\t [--help | -h]                        Show usage"
		print "\t [--list-tests | -l]                  Print out all registered tests."
		print "\t [--run-only | -r 'testname']         Run a specific test"
		print "\t [--verbose | -V]                     Verbose output"
		print "\t [--run-only-pattern | -p 'string']   Run only tests containing the string value"
		print "\n\tExample: Run only the test 'start_top'"
		print "\t\t python ./regression.py --run-only start_stop"
		print "\n\tExample: Run only the tests with the string 'systemd' present in them"
		print "\t\t python ./regression.py --run-only-pattern systemd"


def main(argv):
	lrmd_loc = argv[0].replace("regression.py", "lrmd")
	test_loc = argv[0].replace("regression.py", "lrmd_test")

	o = TestOptions()
	o.build_options(argv)

	tests = Tests(lrmd_loc, test_loc, o.options['verbose'])
	tests.build_generic_tests()
	tests.build_custom_tests()
	tests.setup_test_environment()

	print "Starting ..."

	if o.options['list-tests']:
		tests.print_list()
	elif o.options['show-usage']:
		o.show_usage()
	elif o.options['run-only-pattern'] != "":
		tests.run_tests_matching(o.options['run-only-pattern'])
		tests.print_results()
	elif o.options['run-only'] != "":
		tests.run_single(o.options['run-only'])
		tests.print_results()
	else:
		tests.run_tests()
		tests.print_results()

	tests.cleanup_test_environment()

if __name__=="__main__":
	main(sys.argv)
