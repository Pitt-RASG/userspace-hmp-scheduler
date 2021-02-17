# Userspace HMP scheduler

```
~/userspace-hmp-scheduler $ ./report cpu_cycles,inst_retired benchmarks/cpu
cpu_cycles:0	inst_retired:0	
cpu pid: 18396
cpu_cycles:2155770407	inst_retired:2209359151	
cpu_cycles:4358543448	inst_retired:4465696513	
cpu_cycles:6561742801	inst_retired:6722480356	
cpu_cycles:8765007878	inst_retired:8979323403	
cpu_cycles:10968455756	inst_retired:11236354721	
cpu_cycles:13171625441	inst_retired:13493100165	
cpu_cycles:15375124515	inst_retired:15750190744	
cpu_cycles:17578436177	inst_retired:18007090848

~/userspace-hmp-scheduler $ ./report cpu_cycles,inst_retired,l1d_cache_refill benchmarks/ll_standard
cpu_cycles:0	inst_retired:0	l1d_cache_refill:0	
starting
cpu_cycles:492896064	inst_retired:577613365	l1d_cache_refill:2922820	
cpu_cycles:1041508715	inst_retired:1204574771	l1d_cache_refill:6228027	
done
120001200
```
