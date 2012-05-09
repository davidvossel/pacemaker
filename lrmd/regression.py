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

	def add_cmd(self, cmd):
		self.cmds.append(cmd)

	def get_exitcode(self):
		return self.result_exitcode

	def print_result(self):
		print "    %s" % self.result_txt

	def run_cmd(self, args):
		cmd = shlex.split(args)
		cmd.insert(0, self.test_tool_location)
		test = subprocess.Popen(cmd)
		test.wait()
		return test.returncode

	def run(self):
		res = 0
		print "\nBEGIN LRMD TEST %s " % self.name
		self.result_txt = "SUCCESS - '%s'" % (self.name)
		self.result_exitcode = 0
		lrmd = subprocess.Popen("./lrmd")
		time.sleep(1) # terrible
		for cmd in self.cmds:
			res = self.run_cmd(cmd)
			if res != 0:
				self.result_txt = "FAILURE - '%s' on cmd '%s'" % (self.name, cmd)
				self.result_exitcode = res
				break
		print "RESULT: %s\nEND LRMD LRMD TEST %s \n" % (self.name, self.result_txt)
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
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")

		### start/stop test ###
		test = self.new_test("start/stop", "Register a test, the start and stop it");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")

		### monitor test ###
		test = self.new_test("monitor_test", "Register a test, the start, monitor a few times, then stop");
		test.add_cmd("-c register_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:0 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"start\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:start rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"monitor\" "
			"-i \"1000\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:monitor rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c exec "
			"-r \"test_rsc\" "
			"-a \"stop\" "
			"-l \"NEW_EVENT event_type:2 rsc_id:test_rsc action:stop rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")
		test.add_cmd("-c unregister_rsc "
			"-r \"test_rsc\" "
			"-C \"ocf\" "
			"-P \"pacemaker\" "
			"-T \"Dummy\" "
			"-l \"NEW_EVENT event_type:1 rsc_id:test_rsc action:(null) rc:0 exec_rc:0 call_id:1 op_status:0\" "
			"-t 1000")


		### multi monitor and cancel test ###

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
