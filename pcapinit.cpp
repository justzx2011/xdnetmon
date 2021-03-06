/*
 * File: pcapinit.cpp 
 * Author: Zhang Xijin(mfs6174)
 * Email: mfs6174@gmail.com
 *
 * Copyright (C) 2011 Zhang Xijin(mfs6174)
 *
 * This file is part of XDnetmon.
 *
 * XDnetmon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * XDnetmon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XDnetmon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "include.h"
#include "h.h"
#define ETHER_ADDR_LEN 6
const int changdu=60;//抓包最大的长度,期望减少复制工作,增加效率
const int Ethchangdu=14; //以太头长度
char ebuf[PCAP_ERRBUF_SIZE];//错误信息缓冲
/*
string filter_exp = " (ip net 192.168.0.0 mask 255.255.252.0) and (not (net 202.117.112.0 mask 255.255.240.0)) and (not (net 222.25.128.0 mask 255.255.192.0)) and (not (net 115.155.0.0 mask 255.255.192.0)) and (not (net 219.245.64.0 mask 255.255.192.0)) and (not (net 219.244.112.0 mask 255.255.240.0)) and (not (net 210.27.0.0 mask 255.255.240.0)) and (not ( (dst net 192.168.0.0 mask 255.255.252.0) and (src net 192.168.0.0 mask 255.255.252.0) ) ) "; 旧的固定过滤表达式
*/
extern struct Shezhi shezhi;
struct bpf_program fp;
bpf_u_int32 mask; /* 网 络掩码 */
bpf_u_int32 net; /* IP */
struct pcap_pkthdr header; /* pcap头 */
const u_char *packet; /* 数据包 */
//string dev(shezhi.zhiding?(shezhi.dev):(pcap_lookupdev(ebuf))); //这句有问题,导致设备不对?
pcap_t *pp; //pcap句柄


//开始包转换部分

/* 以太网头 */
struct sniff_ethernet {
	u_char ether_dhost[ETHER_ADDR_LEN]; /* 目的地址 */
	u_char ether_shost[ETHER_ADDR_LEN]; /* 源地址 */
	u_short ether_type; /* IP? ARP? RARP? 等 */
};

/* IP 头 */
struct sniff_ip {
	u_char ip_vhl; /* version << 4 | header length >> 2 */
	u_char ip_tos; /* type of service */
	u_short ip_len; /* total length */
	u_short ip_id; /* identification */
	u_short ip_off; /* fragment offset field */
#define IP_RF 0x8000 /* reserved fragment flag */
#define IP_DF 0x4000 /* dont fragment flag */
#define IP_MF 0x2000 /* more fragments flag */
#define IP_OFFMASK 0x1fff /* mask for fragmenting bits */
	u_char ip_ttl; /* time to live */
	u_char ip_p; /* protocol */
	u_short ip_sum; /* checksum */
	struct in_addr ip_src,ip_dst; /* source and dest address */
};
#define IP_HL(ip) (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip) (((ip)->ip_vhl) >> 4)

sniff_ip *dangip; //IP头指针
sniff_ethernet *dangeth; //以太头指针

string genfilter()
{
  string ftstr("");
  if (shezhi.dff)
  {
    ftstr=" ip and (net 192.168.0.0 mask 255.255.252.0) and (not (net 202.117.112.0 mask 255.255.240.0)) and (not (net 222.25.128.0 mask 255.255.192.0)) and (not (net 115.155.0.0 mask 255.255.192.0)) and (not (net 219.245.64.0 mask 255.255.192.0)) and (not (net 219.244.112.0 mask 255.255.240.0)) and (not (net 210.27.0.0 mask 255.255.240.0)) and (not ( (dst net 192.168.0.0 mask 255.255.252.0) and (src net 192.168.0.0 mask 255.255.252.0) ) ) "; //默认过滤表达式
    return ftstr;
  }
  else
  {
    ifstream ftfile(shezhi.ftfile.c_str());
    if (ftfile.fail())
    {
      cout<<"打开自定义内网ip段文件失败，请检查该文本文件：\""<<shezhi.ftfile<<"\"的存在性"<<endl;
      tuichu(-1);
    }
    string rd,dnet,dmsk,rnet,rmsk;
    ftfile>>rd;
    if (rd!="s"&&rd!="g")
    {
      cout<<"识别工作模式选项失败，无法识别：\""<<rd<<"\",请在文件第一行写\"s\"代表单机模式，或者\"g\"代表网关模式"<<endl;
      tuichu(-1);
    }
    ftfile>>rnet>>rmsk;
    ftstr="ip and ";
    if (rd=="g")
      ftstr+="(net "+rnet+" mask "+rmsk+" )";
    else
    {
      // 以下来自http://www.cnblogs.com/chenyadong/archive/2011/10/19/2217762.html
      // 只知道ioctl是个内核函数,但是不明白如何用的
      // 页面上有解释
      // 有修改 修改错误处理方式 以下第6行处
      int inet_sock,rt;
      struct ifreq ifr;
      inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
      strcpy(ifr.ifr_name, shezhi.dev.c_str());
      //SIOCGIFADDR标志代表获取接口地址
      rt=ioctl(inet_sock, SIOCGIFADDR, &ifr);
      if (rt!=0)
      {
        cout<<"在指定的网络设备 "<<shezhi.dev<<" 上获取ip地址失败,请检查设置和网络连接"<<endl;
        tuichu(-1);
      }
      dnet=inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr);  
      //抄的获取网卡ip的部分结束
      cout<<"工作于单网卡模式，本机IP为 "<<dnet<<endl;
      ftstr+="(host "+dnet+")";
    }
    while (ftfile>>dnet>>dmsk)
      ftstr=ftstr+" and (not (net "+dnet+" mask "+dmsk+") )";
    ftstr=ftstr+" and (not ( (dst net "+rnet+" mask "+rmsk+") and (src net "+rnet+" mask "+rmsk+") ) ) ";
    return ftstr;
  }
}

void pcapinit() //初始化函数,由main()在程序启动时调用,开启pcap句柄,编译过滤表达式,设置过滤器
{
  pp=pcap_open_live(shezhi.dev.c_str(),changdu,0,0,ebuf);
  pcap_lookupnet(shezhi.dev.c_str(),&net,&mask,ebuf);
  string filter_exp=genfilter();
  //cout<<filter_exp<<endl;
  pcap_compile(pp, &fp, filter_exp.c_str(), 1, net);
  pcap_setfilter(pp, &fp);
}

void uc2mac(string &rr,u_char ss[])//将usighed char类型的mac地址转换为6段16进制可读字符串,rr存储结果字符串,ss是待转换的字符数组
{
  static char macfu[18]="0123456789ABCDEF";
  rr="";
  int i;
  rr.push_back(macfu[ss[0]>>4]);
  rr.push_back(macfu[ss[0]%16]);
  for (i=1;i<ETHER_ADDR_LEN;i++)
  {
    //分别取出unsigned char对应16进制的两位,存入字符串
    rr.push_back(':');
    rr.push_back(macfu[ss[i]>>4]);
    rr.push_back(macfu[ss[i]%16]);
  }
}
    
void zuoid(string &s,bool fl) //转换mac和ip为一个id字符串,s是存放结果的字符串,fl是标志,为true时源方向是内网计算机
{
  string ss;
  if (fl)
  {
    uc2mac(s,dangeth->ether_shost);//取出以太头中的源mac,转换为可读形式放入s
    ss=inet_ntoa(dangip->ip_src);//取出IP头中的源IP,转换为点分10进制的字符数组,在赋值给字符串ss
    s=s+"##"+ss;//用##标志分隔mac与IP
  }
  else
  {
    uc2mac(s,dangeth->ether_dhost);
    ss=inet_ntoa(dangip->ip_dst);
    s=s+"##"+ss;
  }
}

void huidiao(u_char *args, const struct pcap_pkthdr *tou,const u_char *bao)//回调函数，解析数据包，写入map
  //pcap loop的回调函数,第一个为参数,未使用,tou是抓到数据包的头指针,bao是实际包数据块的指针
{
  string id="";//初始化存储ID的字符串类
  bool fl=false;
  static bpf_u_int32 pan=net&mask;//静态变量,判定标志,即内网的掩码和IP段的按位与
  dangeth=(struct sniff_ethernet *)(bao);//强制转换数据块指针为以太头指针,即取最前的一块数据对应以太头的结构
  // if ( ((dangeth->ether_type)==0x0806)||((dangeth->ether_type)==0x8035))
  // {
  //   //doarp(bao);//准备对arp和rarp包进行处理,函数未编写
  //   return;
  // }
  //  cout<<(dangeth->ether_type)<<endl;
  // if ( (dangeth->ether_type)!=0x0800)//抛弃非IP包
  //   return;
  dangip=(struct sniff_ip *)(bao+sizeof(sniff_ethernet));//强制转换数据块指针加上以太头的偏移量为IP头
  if (((dangip->ip_src.s_addr)&mask)==pan)//卧槽,这个==前不加括号就他喵的不对,这是神马神运算顺序!//于是判断哪个方向是内网,设置传送给zuoid()的fl
  {
    fl=true;
  }
  else
  {
    fl=false;
  }
  zuoid(id,fl);//生成mac和IP组合对应的ID字符串
  pushmap(id,tou->len);//调用map.cpp中的pushmap(),将这个包的大小存入缓存map
  if (!shezhi.wat)
    return;
  int ipl=IP_HL(dangip)*4;
  if (ipl<20)
    return;
  if (fl)//传入IP偏移以后的指针,继续解包
    dosnif(bao+sizeof(sniff_ethernet)+ipl,dangip->ip_dst.s_addr,dangip->ip_p);
  else
    dosnif(bao+sizeof(sniff_ethernet)+ipl,dangip->ip_src.s_addr,dangip->ip_p);
}



  
