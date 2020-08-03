#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <zlib.h>
#include <pthread.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"

//10秒没访问就是非热点文件
#define NONHOT_TIME 10
#define INTERVAL_TIME 30//非热点的检测每30秒一次
#define BACKUP_DIR "./backup/" //文件的备份路径
#define GZFILE_DIR "./gzfile/" //压缩包存放路径
#define DATA_FILE "./list.backup"//数据管理模块的数据备份文件名称


namespace _cloud_sys{
	class FileUtil{
		public:
			//从文件中读取所有内容
			static bool Read(const std::string& name,std::string* body){
				//一定要注意以二进制方式打开文件
				std::ifstream fs(name,std::ios::binary);//输入文件流打开
				if (fs.is_open()==false){
					std::cout<<"open file"<<name<<"failed\n";
					return false;
				}
				//boost::filesystem::file_size(name)获取文件大小
				int64_t fsize=boost::filesystem::file_size(name);
				//给body申请空间
				body->resize(fsize);
				//读取fsize长度的内容到body中,(char*,int)
				fs.read(&(*body)[0],fsize);//body是指针，需要先解引用
				//判断上一次操作是否成功
				if (fs.good()==false){
					std::cout<<"file "<<name<<" read data failed!\n";
					return false;
				}
				//关闭文件
				fs.close();
				return true;
			}
			//向文件中写入数据
			static bool Write(const std::string& name,const std::string &body){
				//输出流--ofstream默认打开文件的时候会清空原有内容
				//当前策略是覆盖写入
				std::ofstream ofs(name,std::ios::binary);//输出流
				if (ofs.is_open()==false){
					std::cout<<"open file"<<name<<"failed\n";
					return false;
				}
				//向文件中写入数据
				ofs.write(&body[0],body.size());
				if (ofs.good()==false){
					std::cout<<"file "<<name<<" write data failed!\n";
					return false;
				}
				//关闭文件
				ofs.close();
				return true;
			}
	};
	
	
	class CompressUtil
	{
		public:
			//文件压缩  原文件名称，压缩包名称
			static bool Compress(const std::string& src,const std::string &dst){
				std::string body;
				FileUtil::Read(src,&body);

				gzFile gf=gzopen(dst.c_str(),"wb");//打开压缩包
				if(gf==NULL){
					std::cout<<"open file "<<dst<<" failed!\n";
					return false;
				}
				//将原文件压缩写入打开的dst中
				int wlen=0;
				while(wlen<body.size()){//防止body中的数据没有压缩完
					int ret=gzwrite(gf,&body[wlen],body.size()-wlen);
						if(ret==0){
							std::cout<<"file "<<dst<<" write compress data failed!\n";
							return false;
						}
					wlen+=ret;
				}
				gzclose(gf);
				return true;
			}
			//文件解压缩 压缩包名称,原文件名称
			static bool UnCompress(const std::string& src,const std::string& dst){
				std::ofstream ofs(dst,std::ios::binary);
				if(ofs.is_open()==false){
					std::cout<<"open file "<<dst<<"failed!\n";
					return false;
				}
				gzFile gf=gzopen(src.c_str(),"rb");
				if(gf==NULL){
					std::cout<<"open file "<<src<<"failed!\n";
					ofs.close();
					return false;
				}
				int ret;
				char tmp[4096]={0};
				//gzread(句柄，缓冲区,缓冲区大小)
				//返回实际读取到的解压后的数据大小
				while((ret=gzread(gf,tmp,4096))>0){
					//写入ret大小的数据到原文件中
					ofs.write(tmp,ret);
				}
				ofs.close();
				gzclose(gf);
				return true;
			}
	};
//数据管理
	class DataManager
	{
		public:
			DataManager(const std::string& path)
				:_back_file(path)
			{
			//读写锁的初始化
				pthread_rwlock_init(&_rwlock,NULL);
			}
			~DataManager(){
			//读写锁的销毁
				pthread_rwlock_destroy(&_rwlock);
			}
			//判断文件是否存在
			bool Exists(const std::string& name){
				//是否能够从_file_list找到这个文件信息
				//在访问临界资源的时候加读锁
				pthread_rwlock_rdlock(&_rwlock);
				auto it=_file_list.find(name);
				//没找到
				if(it==_file_list.end()){
					pthread_rwlock_unlock(&_rwlock);
					return false;
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			
			//判断文件是否已经压缩
			bool IsCompress(const std::string& name){
				//管理的数据：原文件名称，压缩包名称
				//文件上传之后，原文件名称和压缩包名称一致
				//文件压缩后，将压缩包名称更新为具体的包名
				pthread_rwlock_rdlock(&_rwlock);
				auto it=_file_list.find(name);
				if(it==_file_list.end()){
					pthread_rwlock_unlock(&_rwlock);
					return false;//文件不存在
				}
				if(it->first==it->second){
					pthread_rwlock_unlock(&_rwlock);
					return false;//两个名称一致，文件未被压缩
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			//获取未压缩文件列表
			bool NonCompressList(std::vector<std::string>* list){
				//遍历_file_list;将没有压缩的放到list中
				pthread_rwlock_rdlock(&_rwlock);
				for(auto it=_file_list.begin();it!=_file_list.end();++it){
					if(it->first==it->second){
						list->push_back(it->first);
					}
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}
			//插入或更新数据
			bool Insert(const std::string &src,const std::string& dst){
				//更新修改需要加写锁
				pthread_rwlock_wrlock(&_rwlock);
				_file_list[src]=dst;
				pthread_rwlock_unlock(&_rwlock);
				//更新修改之后重新存储文件名
				Storage();
				return true;
			}
			//获取所有文件名称,用于向外展示文件列表使用
			bool GetAllName(std::vector<std::string>* list){
				pthread_rwlock_rdlock(&_rwlock);
				auto it=_file_list.begin();
				for(;it!=_file_list.end();++it){
					list->push_back(it->first);//获取原文件名称
				}
				pthread_rwlock_unlock(&_rwlock);
				return true;
			}

			//根据源文件名称获取压缩包名称
			bool GetGzName(const std::string &src,std::string *dst){
				auto it=_file_list.find(src);
				//没找到源文件名称
				if(it==_file_list.end()){
					return false;
				}
				//找到了，返回压缩包名称
				*dst=it->second;
				return true;
			}

			//数据改变后持久化存储-管理的是文件名
			//向指定的路径中写入需要管理的原文件名称-压缩包名称
			bool Storage(){
				//将_file_list中的数据进行持久化存储
				//数据对象进行持久化存储---序列化
				//src dst\r\n
				std::stringstream tmp;//实例化一个stream流对象
				pthread_rwlock_rdlock(&_rwlock);
				auto it=_file_list.begin();
				for(;it!=_file_list.end();++it){
					tmp<<it->first<<" "<<it->second<<"\r\n";
				}
				//将tmp中内容写入_back_file中
				pthread_rwlock_unlock(&_rwlock);
				//清空写入的
				FileUtil::Write(_back_file,tmp.str());//tmp.str()是将stream流对象转化晨string对象再写入
				return true;
			}
			//启动时初始化加载原有数据
			//持久化存储文件名格式：filename gzfilename\r\nfilename gzfilename\r\n
			bool InitLoad(){
				//从数据的持久化存储文件中加载数据
				//1.将这个备份文件中的数据读取出来
				std::string body;
				if(FileUtil::Read(_back_file,&body)==false){
					return false;
				}
				//2.进行字符串处理，按照\r\n进行分割
				//boost::split(vector,src,sep,flag)
				std::vector<std::string> list;
				//将body中的内容按\r\n进行分割，不保留\r\n，分割后存放到list中
				boost::split(list,body,boost::is_any_of("\r\n"),boost::token_compress_off);

				//3.每一行按照空格进行分割-前边是key，后边是val
				for(auto i:list){
					size_t pos=i.find(" ");
					if(pos==std::string::npos){
						continue;
					}
					std::string key=i.substr(0,pos);
					std::string val=i.substr(pos+1);
					//4.将key/val添加到_file_list中
					Insert(key,val);
				}
				return true;
			}
		private:
			std::string _back_file;//持久化数据存储
			std::unordered_map<std::string,std::string> _file_list;//数据管理容器
			pthread_rwlock_t _rwlock;//读写锁

	};

	_cloud_sys::DataManager data_manage(DATA_FILE);

//非热点文件压缩
	class NonHotCompress
	{
		public:
			NonHotCompress(const std::string gz_dir,const std::string bu_dir)
				:_gz_dir(gz_dir)
				,_bu_dir(bu_dir)
			{}
			//总体向外提供的功能接口，开始压缩模块
			bool Start(){
				//是一个循环的，持续的过程-每隔一段时间，判断有没有非热点文件，进行压缩
				while(1){
					//1.获取一下所有的未压缩的文件列表
					std::vector<std::string> list;
					data_manage.NonCompressList(&list);
					//2.逐个判断这个文件是否是热点文件
					for(int i=0;i<list.size();++i){
						bool ret=FileIsHot(list[i]);//判断是否是热点文件
						if(ret==false){
							std::cout<<"non hot file "<<list[i]<<"\n";
							//非热点文件则组织原文件的路径名称以及压缩包的路径名称
							//进行压缩存储
							std::string s_filename=list[i];//纯原文件名称
							std::string d_filename=list[i]+".gz";//纯压缩包名称
							std::string src_name=_bu_dir+s_filename;//原文件名称路径名称
							std::string dst_name=_gz_dir+d_filename;//压缩文件路径名称
							//3.如果是非热点文件，则压缩这个文件，删除原文件
							if(CompressUtil::Compress(src_name,dst_name)==true){
								data_manage.Insert(s_filename,d_filename);//更新数据信息
								unlink(src_name.c_str());//删除原文件
							}
						}
					}
					//4.休眠一会
					sleep(INTERVAL_TIME);
				}
				return true;
			}
		private:
			//判断一个文件是否是热点文件
			bool FileIsHot(const std::string& name ){
				//问题：什么文件是非热点文件--当前时间减去最后一次访问时间>n秒
				time_t cur_t=time(NULL);//获取当前时间
				struct stat st;
				if(stat(name.c_str(),&st)<0){
					std::cout<<"get file"<<name<<"stat failed!\n";
					return false;
				}
				if((cur_t-st.st_atime)>NONHOT_TIME){
					return false;//非热点返回false
				}
				return true;//NONHOT_TIME以内都是热点文件
			}
		private:
			std::string _bu_dir;//压缩前文件的存储路径
			std::string _gz_dir;//压缩后文件的存储路径
	};
//服务器
	class Server
	{
		public:
			Server(){
			}
			~Server(){
			}
			//启动网络通信模块接口
			bool Start(){
				_server.Put("/(.*)",Upload);
				_server.Get("/list",List);
				//.*表示匹配任意字符串，()表示捕捉这个字符串，是正则表达式
				_server.Get("/download/(.*)",Download);//为了避免有文件名叫list与list请求混淆
				
				_server.listen("0.0.0.0",9000);//搭建tcp服务器，进行http数据接收处理 
				return true;
			}
		private:
			//文件上传处理回调函数
			static void Upload(const httplib::Request &req,httplib::Response &rsp){
				//req.method-解析出的请求方法
				//req.path-解析出的请求资源路径
				//req.headers--这是一个头部信息键值对
				//req.body-存放请求数据的正文 
				// /(.*)  例如/test.txt
				// matches[0]=/test.txt
				// matches[1]=test.txt
				std::string filename=req.matches[1];//纯文件名称
				std::string pathname=BACKUP_DIR+filename;//文件存放的路径名
				FileUtil::Write(pathname,req.body);//存储文件
				data_manage.Insert(filename,filename);//文件名放到数据管理模块中
				rsp.status=200;
				return;
			}
			//文件列表请求回调函数
			static void List(const httplib::Request &req,httplib::Response &rsp){
				//通过data_manage数据管理对象获取文件名称列表
				std::vector<std::string> list;
				data_manage.GetAllName(&list);
				//组织相应html网页数据
				std::stringstream tmp;
				tmp<<"<html><body><hr />";//标头
				for(int i=0;i<list.size();i++){
					tmp<<"<a href='/download/"<<list[i]<<"'>"<<list[i]<<"</a>";
					//tmp<< "<a href='download/a.txt'> a.txt </a>" 
					//这是一个超链接,用户点击之后,服务器发送href=后面的连接请求(/download/a.txt)
					//而中间的a.txt是用来展示的
					tmp<<"<hr />";//这是一个分割线标签
				}
				tmp<<"<hr /></body></html>";
				
				//填充rsp的正文与状态码还有头部信息
				//set_content(正文数据，正文数据长度，正文类型-Content-Type):
				rsp.set_content(tmp.str().c_str(),tmp.str().size(),"text/html");
				rsp.status=200;
				return ;
			}
			//文件下载处理回调函数
			static void Download(const httplib::Request &req,httplib::Response &rsp){
				//1.从数据模块中判断文件是否存在
				std::string filename=req.matches[1];//前面路由注册时捕捉的.*---就是文件名
				if(data_manage.Exists(filename)==false){
					rsp.status=404;
					return ;
				}
				//2.判断文件是否已经压缩，压缩了则要先解压缩，然后再读取文件数据
				std::string pathname=BACKUP_DIR+filename;//源文件路径名
				if(data_manage.IsCompress(filename)!=false){
					//文件被压缩，先将文件解压出来
					std::string gzfile;//获取文件压缩包的名称
					data_manage.GetGzName(filename,&gzfile);
					std::string gzpathname=GZFILE_DIR+gzfile;//组织一个压缩包的路径名
					CompressUtil::UnCompress(gzpathname,pathname);//将压缩包解压
					unlink(gzpathname.c_str());//删除压缩包
					data_manage.Insert(filename,filename);//更新数据信息
				}
				//从文件中读取数据，响应给客户端
				FileUtil::Read(pathname,&rsp.body);//直接将文件数据读取到rsp的body中
				rsp.set_header("Content-Type","application/octet-stream");//二进制流文件下载
				rsp.status=200;
				return ;
			}

		private:
			std::string _file_dir;//文件上传备份路径
			httplib::Server _server;
	};
}
