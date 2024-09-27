# 凤梨数据库 FongleeDB

## 介绍：
这是一个从0开始的数据库项目。 
区别于套壳软件，在这个项目中，你可以看到一条sql命令是如何从解析开始，经历各个步骤，最终实现磁盘持久化的全部过程。 

如果你有天马行空的点子，可以自由在这个项目中尝试，因为它完全透明，不包含任何黑盒模块(除非你想修改操作系统的磁盘IO接口，那你一定要告诉我，因为我也想这么干来着，比如绕开文件系统直接操作一整块磁盘的逻辑地址空间，手动管理磁盘块分配或者多磁盘带宽加速等)。 
我不希望你为了添加某个功能而调用任何外部库，这很让我厌烦。
有兴趣的人可以根据下方的联系方式与我联系，一起闲聊和讨论有关存储技术的乐趣。 

这不是一个功能齐全的项目，它仅仅不到1m，甚至还是个测试版本，因为网络模块还没加入，当加入网络模块后才刚刚能算有一个完整的链路，当前是将命令触发入口写死在TableManagerTest模块的。 
有太多的工作要做，有太多有趣的想法想付诸实践，然而对于一个初见雏形的作品来讲，我实在忍不住想带它出来透透气。 
因为只有我一个人在开发，而且不定时更新(看心情)，速度必然会慢，不过这个不重要，可预见的它是具备长大成人的条件的，尽管这也不是最终目的。 

## 当前具备的sql功能：
基本sql命令： create，select，update，insert，delete，drop，以及查看命令：show tables，show columns from [table name] 
比如：
show tables
create table users (id int32 index, name string(16) not null, password string(20))
show columns from users
insert into users (id, name, password) value (143589, 'x0x0x8x', '5220015')
select * from users where id=500000
delete * from users where name='x0x0x8x'
drop table users

## 后续开发计划：
索引，缓存，表关联，事务支持....太多了，还有一些奇怪的点子... 

## 使用方法：
### 1. 修改initStorageEngine函数参数为你希望存储数据的目录(必须已存在)。
### 2. 编译运行所有源代码就可以了(可以用VS，很方便)，默认是交互模式，也可以改成自动测试场景，修改位置在TableHandleTestTask.c文件中。
### 3. 有很多配置在某些.h文件中，可以自行探索。
### ps: linux暂时运行不了，因为我需要一个linux系统的机器来测试，在虚拟机里的话，文件IO模块的直接磁盘IO会报错。有空再弄，先在win下玩。
### ps2：如果使用过程有疑问，可以直接找我。

## 作者邮箱：
476706568@qq.com 
a476706568@gmail.com 

最后，玩得愉快。 
