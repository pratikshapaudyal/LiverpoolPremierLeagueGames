import unittest, sys, os, pathlib, subprocess, json, yaml
# os.environ["BBCLI_ENV"] = "development"
# import bb

class TestExample(unittest.TestCase):
    """Example Description Of Test - TestExample
         - What will this test?
         - Provide examples of expected & valid data for your tests
    """
    def test_example(self):
        """Test as a python object imported into the project
        """
        self.assertTrue(True, "This was an example test")

if __name__ == "__main__":
    unittest.main()