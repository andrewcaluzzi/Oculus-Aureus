#!/bin/sh
# All demo oracle runs for one build: dam1/dam2 with Dam+ off and on, dam2 on the fork's reference exe. Then compares.
O=/home/dev/out/oracle
S=/home/dev/out/scripts/oracle.sh
sh $S dam1 off ""
sh $S dam1 on 1
sh $S dam2 off ""
sh $S dam2 on 1
[ -f $O/dam2.ref.txt ] || sh $S dam2 ref "" /home/dev/out/ref/ProjectMidas
echo "== dam1 off vs baseline"; cmp $O/dam1.off.txt /home/dev/project/tests/baseline/dam1.a.txt && echo SAME
echo "== dam1 on vs baseline"; cmp $O/dam1.on.txt /home/dev/project/tests/baseline/dam1.a.txt && echo SAME
echo "== dam2 off vs ref"; cmp $O/dam2.off.txt $O/dam2.ref.txt && echo SAME
echo "== dam2 on vs ref"; cmp $O/dam2.on.txt $O/dam2.ref.txt && echo SAME
