include /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/Makefile.param
OPENCV_FLAGS := -lpthread -lrt `pkg-config --libs --cflags hisiv100-opencv`
CC := arm-hisiv300-linux-g++

#LIBDETECT_ZK_PATH := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/libdetect_zk
#LIBDETECT_TEACHER_PATH := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/libdetect_teacher
#TEACHER_DET_SRC = $(wildcard $(LIBDETECT_TEACHER_PATH/*.cpp))
#TEACHER_DET_OBJ = $(TEACHER_DET_SRC:%.cpp=%.o)
#LIBKVCONFIG_PATH := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/libKVConfig
LIBKVCONFIG_PATH := $(MPP_PATH)/sample/libKVConfig

SRC = $(wildcard *.cpp)
OBJ = $(SRC:%.cpp=%.o)
INC_PATH := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/include/
REL_LIB := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/lib
MPI_LIBS := $(REL_LIB)/libmpi.a
IVE_LIBS := $(REL_LIB)/libive.a
CFLAGS += -I $(INC_PATH)
CFLAGS += -I $(LIBKVCONFIG_PATH)
#KVCONFIG_PATH := /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/libdetect_stu/libkvconfig
#CFLAGS := -I $(KVCONFIG_PATH)
#CFLAGS += -I $(LIBDETECT_ZK_PATH)
#CFLAGS += -I $(LIBDETECT_TEACHER_PATH)
all: $(OBJ)

$(OBJ): %.o: %.cpp
	$(CC) $(OPENCV_FLAGS) $(CFLAGS) $(MPI_LIBS) $(IVE_LIBS) -pthread -lm -c  $< -o $@

clean:
	rm *.o
detect:
	@cd /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/detect_with_of;make;
install:
	@cp /root/3516a/Hi3516A_SDK_V1.0.4.0/mpp/sample/detect_with_of/detect_with_of ~/hi3516c/nfs
echo:
	@echo $(MPP_PATH)
