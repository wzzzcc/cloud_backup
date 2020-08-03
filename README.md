# cloud_backup这是一个云备份项目
src目录下是服务器端代码 ：
  包括backup目录和gzfile目录，由于上传时backup为空，所以并没有上传上来;
  包括cloud_backup.cpp、cloud_backup.hpp、httplib.h、makefile、list.backup、cloud_backup（是生成的可执行文件）

cloud_client目录是客户端代码：
   包括cloud_client.cpp、cloud_client.hpp、httplib.h、list.backup（保存上传到服务器端的文件名信息）、backup目录我监控的目录，该目录下的文件需要备份，也就是说把想要备份的文件拖到backup目录下，就会自动备份到服务器端。
   
通过浏览器可以访问到服务器查看备份上的文件列表，并可以下载备份的文件；
  
