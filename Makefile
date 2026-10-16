CXX = g++
CXXFLAGS = -std=c++17

MYSQL_FLAGS = -I.\include -L. -lmysql
PSAPI_FLAGS = -lpsapi

all: activity_monitor.exe

# The helper source files are linked into the one final application.
activity_monitor.exe: activity_monitor.cpp idle_check.cpp process_list.cpp process_stats.cpp
	$(CXX) activity_monitor.cpp idle_check.cpp process_list.cpp process_stats.cpp -o activity_monitor.exe $(MYSQL_FLAGS) $(PSAPI_FLAGS) $(CXXFLAGS) -pthread

test_connection.exe: test_connection.cpp
	$(CXX) test_connection.cpp -o test_connection.exe $(MYSQL_FLAGS) $(CXXFLAGS)

report.exe: report.cpp
	$(CXX) report.cpp -o report.exe $(MYSQL_FLAGS) $(CXXFLAGS)

all_targets: activity_monitor.exe test_connection.exe report.exe

clean:
	del *.exe
