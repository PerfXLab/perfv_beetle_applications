# perfv_beetle_applications

######################################################
  
perfxlab_FaceDetection:
 
        make clean  && make gen && make all && make run

perfxlab_BilinearResize:

        make clean && make gen && make all && make run

perfxlab_CannyEdgeDetection:

        make clean && make -j io=host && make run
        
        
 perfv-beetle 目前支持的配件有：
    4款lcd , 1款camera(分rgb和yuv) , 1款蓝牙模块 ，1款wifi模块
        
 
 4款lcd 及对应的驱动模块如下：

```
不同lcd(尺寸)			分辨率			lcd屏对应的驱动模块			具体模块文件
0.91英寸				 80 x 160			st7735s						display/st7735s/st7735s.c
1.8英寸				128 x 160			st7735s						display/st7735s/st7735s.c
2.8英寸				240 x 320			yt280L030					display/yt280L030/yt280L030.c
3.5英寸				320 x 480			z350it008					display/z350it008/z350it008.c
```

注：

​		目前移植的人工智能的demo，都是基于 2.8英寸的lcd屏。
  

######################################################
