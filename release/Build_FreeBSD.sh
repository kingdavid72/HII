rm -f ../bin/FreeBSD/idock ../obj/FreeBSD/*.o
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/utility.o ../src/utility.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/io_service_pool.o ../src/io_service_pool.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/scoring_function.o ../src/scoring_function.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/atom.o ../src/atom.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/receptor.o ../src/receptor.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/ligand.o ../src/ligand.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/random_forest.o ../src/random_forest.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/random_forest_x.o ../src/random_forest_x.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/random_forest_y.o ../src/random_forest_y.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/log.o ../src/log.cpp -I${BOOST_ROOT} -c
clang++ -std=c++11 -O3 -DNDEBUG -static -o ../obj/FreeBSD/main.o ../src/main.cpp -I${BOOST_ROOT} -c
clang++ -static -O3 -DNDEBUG -o ../bin/FreeBSD/idock ../obj/FreeBSD/utility.o ../obj/FreeBSD/io_service_pool.o ../obj/FreeBSD/scoring_function.o ../obj/FreeBSD/atom.o ../obj/FreeBSD/receptor.o ../obj/FreeBSD/ligand.o ../obj/FreeBSD/random_forest.o ../obj/FreeBSD/random_forest_x.o ../obj/FreeBSD/random_forest_y.o ../obj/FreeBSD/log.o ../obj/FreeBSD/main.o -pthread -L${BOOST_ROOT}/lib -lboost_system -lboost_filesystem -lboost_program_options -L${CUDA_ROOT}/lib64 -lcuda -lcurand