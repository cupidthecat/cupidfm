valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --num-callers=30 \
         --suppressions=valgrind.supp \
         ./cupidfm
