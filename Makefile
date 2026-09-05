CXX = g++
CXXFLAGS = -std=c++17

# Flags only needed for files that use MySQL
MYSQL_FLAGS = -I.\include -L. -lmysql

# Flags only needed for files that check memory (psapi)
PSAPI_FLAGS = -lpsapi

all: activity_monitor.exe

# Main integrated program - needs both MySQL and psapi
activity_monitor.exe: activity_monitor.cpp
	$(CXX) activity_monitor.cpp -o activity_monitor.exe $(MYSQL_FLAGS) $(PSAPI_FLAGS) $(CXXFLAGS)

# Standalone test files
test_connection.exe: test_connection.cpp
	$(CXX) test_connection.cpp -o test_connection.exe $(MYSQL_FLAGS) $(CXXFLAGS)

process_list.exe: process_list.cpp
	$(CXX) process_list.cpp -o process_list.exe $(CXXFLAGS)

process_stats.exe: process_stats.cpp
	$(CXX) process_stats.cpp -o process_stats.exe $(PSAPI_FLAGS) $(CXXFLAGS)

idle_check.exe: idle_check.cpp
	$(CXX) idle_check.cpp -o idle_check.exe $(CXXFLAGS)

# Build everything at once
all_targets: activity_monitor.exe test_connection.exe process_list.exe process_stats.exe idle_check.exe

clean:
	del *.exe


# make                      # builds activity_monitor.exe (the default target)
# make process_list.exe     # builds just process_list.exe
# make process_stats.exe    # builds just process_stats.exe
# make idle_check.exe       # builds just idle_check.exe
# make test_connection.exe  # builds just test_connection.exe
# make all_targets          # builds everything at once
# make clean                # deletes all .exe files
