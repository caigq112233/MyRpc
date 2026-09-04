这表示 zkCli.sh 不在当前 PATH 环境变量中，不代表 ZooKeeper 没有安装。注释 1
先查找脚本实际位置：
find /usr /opt /home -type f -name 'zkCli.sh' 2>/dev/null
常见结果是：
/usr/share/zookeeper/bin/zkCli.sh
如果找到，就使用完整路径：
/usr/share/zookeeper/bin/zkCli.sh -server 127.0.0.1:2181
也可以检查系统安装的 ZooKeeper 包包含哪些文件：
dpkg -l | grep -i zookeeper
dpkg -L zookeeper | grep zkCli
如果没有 zookeeper 包，也可以试：
dpkg -L zookeeperd | grep zkCli
若确实找不到 CLI，但 2181 端口确实在监听，说明可能只有 ZooKeeper 服务端或 Docker 容器在运行。此时先用四字命令查看状态：
echo ruok | nc 127.0.0.1 2181
echo stat | nc 127.0.0.1 2181


ls /myrpc
ls /myrpc/services
ls /myrpc/services/EchoService/Echo/providers

查看zookeeper连接数

echo mntr | nc 127.0.0.1 2181 | grep zk_num_alive_connections

echo stat | nc 127.0.0.1 2181
其中会有类似：
Clients:
 /127.0.0.1:xxxxx[...]
 /127.0.0.1:yyyyy[...]
每一行代表一个客户端连接。
查看每个连接更详细的信息：
echo cons | nc 127.0.0.1 2181
