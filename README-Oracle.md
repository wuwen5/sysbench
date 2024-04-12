--------------------------------------------------------------
Oracle Build steps 
--------------------------------------------------------------

## 安装Oracle客户端

### Oracle客户端下载地址
  https://www.oracle.com/database/technologies/instant-client/downloads.html

### 使用RPM安装方式，将以下RPM上传到服务器: 
 - oracle-instantclient19.22-basic-19.22.0.0.0-1.x86_64.rpm
 - oracle-instantclient19.22-devel-19.22.0.0.0-1.x86_64.rpm

```
yum -y localinstall oracle-instantclient19.22-basic-19.22.0.0.0-1.x86_64.rpm
yum -y localinstall oracle-instantclient19.22-devel-19.22.0.0.0-1.x86_64.rpm
```

### 使用zip 安装方式

```
unzip instantclient-basic-linux.x64-19.22.0.0.0dbru.zip
unzip instantclient-sdk-linux.x64-19.22.0.0.0dbru.zip
```

```
echo "/root/instantclient_19_22" > /etc/ld.so.conf.d/oracle-instantclient.conf
ldconfig
```

## 编译&安装 Sysbench
```
./autogen.sh
#使用--with-oracle 支持oracle， 通过--with-oracle-includes --with-oracle-libs指定Oracle客户端路径 (如未安装mysql客户端驱动,可--without-mysql排除默认的mysql支持)
#--with-oracle-libs=lib路径，如rpm安装方式，可指定为/usr/lib/oracle/19.22/client64/lib
#--with-oracle-includes=include路径，如rpm安装方式，可指定为/usr/include/oracle/19.22/client64
./configure --with-oracle --with-oracle-libs=/root/instantclient_19_22 --with-oracle-includes=/root/instantclient_19_22/sdk/include --without-mysql
make -j
make install
```

## 验证使用
- 查看帮助信息，是否有oracle相关的选项，以及是否支持oracle
```shell
sysbench --help
```
```
Usage:
  sysbench [options]... [testname] [command]

Commands implemented by most tests: prepare run cleanup help

General options:
  --threads=N                     number of threads to use [1]
  --events=N                      limit for total number of events [0]
  --time=N                        limit for total execution time in seconds [10]
  --warmup-time=N                 execute events for this many seconds with statistics disabled before the actual benchmark run with statistics enabled [0]
  --forced-shutdown=STRING        number of seconds to wait after the --time limit before forcing shutdown, or 'off' to disable [off]
  --thread-stack-size=SIZE        size of stack per thread [64K]
  --thread-init-timeout=N         wait time in seconds for worker threads to initialize [30]
  --rate=N                        average transactions rate. 0 for unlimited rate [0]
  --report-interval=N             periodically report intermediate statistics with a specified interval in seconds. 0 disables intermediate reports [0]
  --report-checkpoints=[LIST,...] dump full statistics and reset all counters at specified points in time. The argument is a list of comma-separated values representing the amount of time in seconds elapsed from start of test when report checkpoint(s) must be performed. Report checkpoints are off by default. []
  --debug[=on|off]                print more debugging info [off]
  --validate[=on|off]             perform validation checks where possible [off]
  --help[=on|off]                 print help and exit [off]
  --version[=on|off]              print version and exit [off]
  --config-file=FILENAME          File containing command line options
  --luajit-cmd=STRING             perform LuaJIT control command. This option is equivalent to 'luajit -j'. See LuaJIT documentation for more information

Pseudo-Random Numbers Generator options:
  --rand-type=STRING   random numbers distribution {uniform, gaussian, pareto, zipfian} to use by default [uniform]
  --rand-seed=N        seed for random number generator. When 0, the current time is used as an RNG seed. [0]
  --rand-pareto-h=N    shape parameter for the Pareto distribution [0.2]
  --rand-zipfian-exp=N shape parameter (exponent, theta) for the Zipfian distribution [0.8]

Log options:
  --verbosity=N verbosity level {5 - debug, 0 - only critical messages} [3]

  --percentile=N       percentile to calculate in latency statistics (1-100). Use the special value of 0 to disable percentile calculations [95]
  --histogram[=on|off] print latency histogram in report [off]

General database options:

  --db-driver=STRING  specifies database driver to use ('help' to get list of available drivers)
  --db-ps-mode=STRING prepared statements usage mode {auto, disable} [auto]
  --db-debug[=on|off] print database-specific debug information [off]


Compiled-in database drivers:
  oracle - Oracle driver

oracle options:
  --oracle-user=STRING     Oracle user [SYSDBA]
  --oracle-password=STRING Oracle password [SYSDBA]
  --oracle-db=STRING       Oracle database name [sbtest]

Compiled-in tests:
  fileio - File I/O test
  cpu - CPU performance test
  memory - Memory functions speed test
  threads - Threads subsystem performance test
  mutex - Mutex performance test

See 'sysbench <testname> help' for a list of options for each test.
```

- 测试初始化数据
```shell
# 测试初始化数据
sysbench oltp_point_select --tables=10 --table-size=10000 --db-driver=oracle --oracle-db=192.168.x.x:1521/orcl --oracle-user=XXX --oracle-password=XXX prepare

# 测试Point_Select **(注意：如发生段错误(Segmentation fault)，需通过--thread-stack-size指定合适线程栈大小，如 --thread-stack-size=512K)**
sysbench oltp_point_select --tables=10 --table-size=10000 --db-driver=oracle --oracle-db=192.168.x.x:1521/orcl --oracle-user=XXX --oracle-password=XXX --report-interval=10 --thread-stack-size=512K run

#清理测试数据
sysbench oltp_point_select --tables=10 --db-driver=oracle --oracle-db=192.168.x.x:1521/orcl --oracle-user=XXX --oracle-password=XXX cleanup
```