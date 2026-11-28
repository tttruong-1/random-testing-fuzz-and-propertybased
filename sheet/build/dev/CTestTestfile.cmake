# CMake generated Testfile for 
# Source directory: /home/tttruong/Downloads/se-random-testing-template/sheet
# Build directory: /home/tttruong/Downloads/se-random-testing-template/sheet/build/dev
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit]=] "/home/tttruong/Downloads/se-random-testing-template/sheet/build/dev/sheet_tests")
set_tests_properties([=[unit]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/tttruong/Downloads/se-random-testing-template/sheet/CMakeLists.txt;99;add_test;/home/tttruong/Downloads/se-random-testing-template/sheet/CMakeLists.txt;0;")
add_test([=[seeds-parse]=] "/home/tttruong/Downloads/se-random-testing-template/sheet/build/dev/sheet_tests" "--seeds" "/home/tttruong/Downloads/se-random-testing-template/sheet/seeds")
set_tests_properties([=[seeds-parse]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/tttruong/Downloads/se-random-testing-template/sheet/CMakeLists.txt;100;add_test;/home/tttruong/Downloads/se-random-testing-template/sheet/CMakeLists.txt;0;")
