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
	def __init__(self, name, description, lrmd_location, test_tool_location):
		self.name = name
		self.description = description
		self.cmds = []
		self.iteration = 0;
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location

		self.result_txt = ""
		self.result_exitcode = 0;

	def add_sys_cmd(self, cmd, args):
		self.cmds.append([cmd, args, 0])

	def add_cmd(self, args):
		args += " -Q"
		self.cmds.append([self.test_tool_location, args, 0])

	def add_expected_fail_cmd(self, args):
		args += " -Q"
		self.cmds.append([self.test_tool_location, args, 255])

	def get_exitcode(self):
		return self.result_exitcode

	def print_result(self):
		print "    %s" % self.result_txt

	def run_cmd(self, args):

		cmd = shlex.split(args[1])
		cmd.insert(0, args[0])
		test = subprocess.Popen(cmd)
		test.wait()
		return test.returncode;

	def run(self):
		res = 0
		i = 1
		print "\n--- BEGIN LRMD TEST %s " % self.name
		self.result_txt = "SUCCESS - '%s'" % (self.name)
		self.result_exitcode = 0
		lrmd = subprocess.Popen("./lrmd")
		for cmd in self.cmds:
			res = self.run_cmd(cmd)
			if res != cmd[2]:
				print "Iteration %d FAILED - pid rc %d expected rc %d- cmd args '%s'" % (i, res, cmd[2], cmd[1])
				self.result_txt = "FAILURE - '%s' failed on cmd iteration %d" % (self.name, i)
				self.result_exitcode = res
				break
			else:
				print "Iteration %d SUCCESS" % (i)
			i = i + 1
		print "--- END LRMD '%s' TEST - %s \n" % (self.name, self.result_txt)
		lrmd.kill()

		return res

class Tests:
	def __init__(self, lrmd_location, test_tool_location):
		self.daemon_location = lrmd_location
		self.test_tool_location = test_tool_location
		self.tests = []

	def new_test(self, name, description):
		test = Test(name, description, self.daemon_location, self.test_tool_location)
		self.tests.append(test)
		return test

	def build_tests(self):
		### register/unregister test ###
		test = self.new_test("registration", "Simple resource registration test");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")

		### start/stop test ###
		test = self.new_test("start/stop", "Register a test, the start and stop it");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")

		### monitor test ###
		test = self.new_test("monitor_test", "Register a test, the start, monitor a few times, then stop");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")

		### monitor and cancel test ###
		test = self.new_test("monitor_and_cancel_test", "Register a test, the start, monitor a few times, then cancel the monitor");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-t \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")

		### monitor fail ###
		test = self.new_test("monitor_fail_test", "Register a test, the start, monitor a few times, then make the monitor fail.");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:none rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" "
			"-t 1000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 2000")
		test.add_sys_cmd("rm", "-f /var/run/Dummy-test_rsc.state")
		test.add_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_DONE\" -t 2000")
		test.add_cmd("-c cancel "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"100\" "
			"-t \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_CANCELLED\" ")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_NOT_RUNNING op_status:OP_DONE\" -t 1000")
		test.add_expected_fail_cmd("-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:OCF_OK op_status:OP_DONE\" -t 1000")

		### get metadata ###
		test = self.new_test("get_metadata", "Retrieve metadata for a resource");
		test.add_cmd("-c metadata "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\"")
		test.add_cmd("-c metadata "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Stateful\"")
		test.add_expected_fail_cmd("-c metadata "
			"-P \"pacemaker\" "
			"-T \"Stateful\"")

	def run_tests(self):
		for test in self.tests:
			test.run()

	def print_results(self):
		failures = 0;
		success = 0;
		print "\n\n======= FINAL RESULTS =========="
		print "\n--- INDIVIDUAL TEST RESULTS:"
		for test in self.tests:
			test.print_result()
			if test.get_exitcode() != 0:
				failures = failures + 1
			else:
				success = success + 1

		print "\n--- TOTALS\n    Pass:%d\n    Fail:%d\n" % (success, failures)


def main(argv):
	print "Start testing!\n"

	tests = Tests("./lrmd", "./lrmd_test");
	tests.build_tests()
	tests.run_tests()
	tests.print_results()

if __name__=="__main__":
	main(sys.argv)
