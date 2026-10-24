# tests —— 单元测试入口
#
# 注意：ctest 在零用例时返回 exit code 8（不是静默通过），
# 所以这里必须始终至少有一个 add_test。

add_subdirectory(unit)
