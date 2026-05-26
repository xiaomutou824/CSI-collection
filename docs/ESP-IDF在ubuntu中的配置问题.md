### 在当前终端窗口中，激活 ESP-IDF 的开发环境

```
source /home/xing/.espressif/v5.5.4/esp-idf/export.sh
```

把 idf.py 的路径加入到系统 PATH（这样你才能找到它）。

激活 ESP-IDF 专用的 Python 虚拟环境（确保依赖库不冲突）。

设置好 ESP32 芯片的交叉编译工具链路径。

每次打开新窗口都要敲这个长长的命令很麻烦。你可以把它加到你的终端配置文件里，一劳永逸：

```
echo "source /home/xing/.espressif/v5.5.4/esp-idf/export.sh" >> ~/.bashrc
```

这样以后每次打开终端，ESP-IDF 环境都会自动激活，省时省力！
