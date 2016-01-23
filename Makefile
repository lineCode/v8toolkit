
all:
	clang++ -std=c++14 -I/usr/local/include -I/users/xaxxon/v8  sample.cpp -o sample /users/xaxxon/v8/out/x64.release/{libv8_base.a,libv8_libbase.a,libicudata.a,libicuuc.a,libicui18n.a,libv8_base.a,libv8_external_snapshot.a,libv8_libplatform.a}
	clang++ -std=c++14 -I/usr/local/include -I/users/xaxxon/v8  toolbox_sample.cpp -o toolbox /users/xaxxon/v8/out/x64.release/{libv8_base.a,libv8_libbase.a,libicudata.a,libicuuc.a,libicui18n.a,libv8_base.a,libv8_external_snapshot.a,libv8_libplatform.a}


clean_docs:
	rm -rf doc	

docs: clean_docs
	doxygen doxygen.cfg
	