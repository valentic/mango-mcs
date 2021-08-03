include $(sort $(wildcard $(BR2_EXTERNAL_TINCAN_PATH)/package/*/*.mk))

target-clean:
	rm -rf $(TARGET_DIR) 
	find $(BASE_DIR) -name '.stamp_target_installed' | xargs rm -rf
	#find $(BASE_DIR) -name '.stamp_host_installed' | xargs rm -rf
	find $(BASE_DIR) -path '*/host-gcc-final*/.stamp_host_installed' | xargs rm -rf
