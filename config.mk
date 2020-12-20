# 定义项目编译的根目录，通过export把某个变量声明为全局的，其他文件中可以使用
# 这里获取当前这个文件所在路径作为根目录
export BUILD_ROOT = $(shell pwd)

# 定义头文件的路径变量
export INCLUDE_PATH = $(BUILD_ROOT)/_include

# 定义我们要编译的目录
BUILD_DIR = $(BUILD_ROOT)/signal/ \
			$(BUILD_ROOT)/proc/    \
			$(BUILD_ROOT)/app/

# 编译时是否成成调试信息，GNU调试器可以利用该信息
export DEBUG = true