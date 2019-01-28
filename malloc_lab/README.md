# 简介
首先写`implicit free list`
即csapp上的示例实现稍加改动
每个内存块的格式为
```
+--------+----------------+-----------------+--------+     
| header | payload        | alignment       | footer |    
+--------+----------------+-----------------+--------+    
| 8      | n              | align n to 8    | 8      |    
+--------+----------------+-----------------+--------+    
```
其中header和footer为
```
+------------------+--------+-------+
| block_total_size | no_use | alloc |
+------------------+--------+-------+
| 61 bits          | 2 bits | 1 bit |
+------------------+--------+-------+
```
注意`block_total_size`是指`header`,`8对齐后的payload大小`,`footer`加起来的总大小
这样组织的原因是：`block_total_size` 一定是8对齐的，如果我们用64位2进制表示它的话，它的最低三位一定是0（1，2，4不能形成8对齐），为了有效地利用
这最低三位，我们把这个块是否已经被占用的存在最低位，0表示空闲，1表示占用，其他两位没有用处。
宏的写法对应以上逻辑
```
/* get the size from a payload pointer */
#define GET_SIZE(ptr) (GET(HDRP(ptr)) & (~0x7))
/* get the alloced or not value from a payload pointer */
#define GET_ALLOC(ptr) (GET(HDRP(ptr)) & (0x1))
```
主要函数作用和思路：

`mm_init`: 申请第一块内存，并放置`prelogue block`和`epilogue block`标记堆的首尾方便遍历，当然维护静态全局变量标记首尾也是可以的，
	   这里我还初始化好了第一块 free block
	   
`place`: 用来放置块的函数，要占用一块空块，需要先考虑是不是要切开这个块（只占用我们需要的那部分，剩余的那部分独立成为一个free block），
         以防止internal fragmentation，这里是否需要切开块的条件可调参。这里实现的时候注意把块的大小，位置都搞明白。
	 
`mm_malloc`:首先要把目标`size`加上`header`,`footer`的大小进行8对齐，然后寻找合适的块（这里搜索的方式可调参，有`first fit`,'next fit`,`best fit')，如果能找到一个空间>=对齐后size的块，就把它放置进去；如果找不到，用`mm_sbrk`移动堆的尾部
	    获得更多内存，放置，并移动`epilogue block`。
	    
`mm_free`:接受一个指针，清理这个指针对应的内存块。除了将header footer的alloc bit设为0， 还需要考虑这个空块是否能与它相领的空块进行合并，否则会产生
          external fragmentation，使用一个`coalesce`函数完成合并的功能。

`coalesce`:接受一个指针，检查指针对应的块的左右两个相邻块是否是空闲块，如果是的话进行合并。这里的实现就是分两边都不是空，左边是空右边不是空……四种情况，
	   需要注意的一个地方是在使用`PUT`摆新块的`header`,`footer`前需要先把总的`size`保存起来，不能`PUT`完再使用`GET_SIZE`相加去获得总`size·
	   ,因为`GET_SIZE`以来原来的`header`,`footer`, `PUT`之后这里面的信息就变了。
	   
`mm_realloc`:接受一个指针和`size`,要求把指针对应的块的大小扩大到`size`,特殊情况怎么处理官方pdf里有些,这里只说扩大怎么做。
	     老规矩先求总大小并对齐，然后像`coalesce`一样考虑，如果相邻的有空闲块并且加在一起的大小能满足要求，我们就不需要申请新内存了，
	     这里需要写个`mm_memcpy`函数用于内存拷贝，实现的时候要判断一下`src`,`dst`的前后顺序并做相应处理防止数据被覆盖。如果没有相邻空闲块能满足要求，
	     这就需要调用`mm_malloc`，并把数据拷过去，把原来的块清除。
	     
基本读懂了书上示例代码就能有思路写，但要完全写对还是要花点时间调试的。。(不要照着书抄啊喂

调试的时候可以在Makefile里加上`-g -O0`，然后用gdb和`printf`调。。可以写个遍历整个block list打印所有信息的函数，各处调用一下。

这个naive的实现得分比较惨淡 `Perf index = 47 (util) + 10 (thru) = 57/100`

接下来优化的方向是减少搜索合适块的时间

下一步是用书上介绍的`explicit double linked free list`,即空闲block的payload部分可以用来存储链表信息。

这样`best_fit`的搜索时间就从所有块变为了所有空闲的块
```
   +--------+----------------+-----------------+----------------------+--------+       
   | header | prev free addr | next free addr  | free payload aligned | footer |    
   +--------+----------------+-----------------+----------------------+--------+    
   | 8      | 8              | 8               | n                    | 8      |    
   +--------+----------------+-----------------+----------------------+--------+    
```
这样子的得分是 `Perf index = 47 (util) + 40 (thru) = 87/100`

`Segregated list`看着好麻烦。。不想写 接下来把链表换成二叉搜索树估计就能有90分了吧。

