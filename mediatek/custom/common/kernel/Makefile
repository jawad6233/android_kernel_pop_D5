include $(MTK_PATH_BUILD)/common.mk
$(call all-modules-src-or-makefile,$(obj),CUSTOM_KERNEL_)
obj-n := dummy.o
#add by llf FR766479 ./makeMtk -t -o=TARGET_BUILD_MMITEST=true r k
ifeq ($(TARGET_BUILD_MMITEST),true)
ccflags-y += -DTARGET_BUILD_MMITEST
endif
