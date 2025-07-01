#!/usr/bin/python3
#
# Copyright 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import errno
import gzip
import os
from socket import *  # pylint: disable=wildcard-import,g-importing-member
import unittest

import gki
import net_test


class KernelFeatureTest(net_test.NetworkTest):
  KCONFIG = None
  AID_NET_RAW = 3004

  @classmethod
  def getKernelConfigFile(cls):
    try:
      return gzip.open("/proc/config.gz", mode="rt")
    except FileNotFoundError:
      return open("/boot/config-" + os.uname()[2], mode="rt")

  @classmethod
  def loadKernelConfig(cls):
    cls.KCONFIG = {}
    with cls.getKernelConfigFile() as f:
      for line in f:
        line = line.strip()
        parts = line.split("=")
        if (len(parts) == 2):
          # Lines of the form:
          # CONFIG_FOO=y
          cls.KCONFIG[parts[0]] = parts[1]

  @classmethod
  def setUpClass(cls):
    super(net_test.NetworkTest, cls).setUpClass()
    cls.loadKernelConfig()

  def assertFeatureAbsent(self, feature_name):
    return self.assertNotIn(feature_name, self.KCONFIG)

  def assertFeatureBuiltIn(self, feature_name):
    return self.assertEqual("y", self.KCONFIG[feature_name])

  def assertFeatureModular(self, feature_name):
    return self.assertEqual("m", self.KCONFIG[feature_name])

  def assertFeatureEnabled(self, feature_name):
    return self.assertIn(self.KCONFIG[feature_name], ["m", "y"])

  def testNetfilterRejectEnabled(self):
    """Verify that CONFIG_IP{,6}_NF_{FILTER,TARGET_REJECT} is enabled."""
    self.assertFeatureBuiltIn("CONFIG_IP_NF_FILTER")
    self.assertFeatureBuiltIn("CONFIG_IP_NF_TARGET_REJECT")

    self.assertFeatureBuiltIn("CONFIG_IP6_NF_FILTER")
    self.assertFeatureBuiltIn("CONFIG_IP6_NF_TARGET_REJECT")

  def testRemovedAndroidParanoidNetwork(self):
    """Verify that ANDROID_PARANOID_NETWORK is gone.

       On a 4.14-q kernel you can achieve this by simply
       changing the ANDROID_PARANOID_NETWORK default y to n
       in your kernel source code in net/Kconfig:

       @@ -94,3 +94,3 @@ endif # if INET
        config ANDROID_PARANOID_NETWORK
               bool "Only allow certain groups to create sockets"
       -       default y
       +       default n
    """
    with net_test.RunAsUidGid(12345, self.AID_NET_RAW):
      self.assertRaisesErrno(errno.EPERM, socket, AF_PACKET, SOCK_RAW, 0)

  @unittest.skipUnless(net_test.IS_GSI, "not GSI")
  def testIsGSI(self):
    pass

  @unittest.skipUnless(gki.IS_GKI, "not GKI")
  def testIsGKI(self):
    pass

  @unittest.skipUnless(not net_test.IS_GSI and not gki.IS_GKI, "GSI or GKI")
  def testMinRequiredKernelVersion(self):
    self.assertTrue(net_test.KernelAtLeast([(4, 19, 236),
                                            (5, 4, 186),
                                            (5, 10, 199),
                                            (5, 15, 136),
                                            (6, 1, 57)]),
                    "%s [%s] is too old." % (os.uname()[2], os.uname()[4]))


if __name__ == "__main__":
  unittest.main()
