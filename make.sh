# Ensure that JAVA_HOME is set to the correct path. You can use something like 'export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64/'.
gcc -shared -fPIC -o mod_zpe.so mod_zpe.c -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux -L${JAVA_HOME}/lib/server -ljvm -I/usr/include/apache2/ -I/usr/include/apr-1.0 -lapr-1 -laprutil-1
