#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mansouri-moradi");
MODULE_DESCRIPTION("OS test");
MODULE_VERSION("1");

static int __init mymodule_init(void){
        pr_info("mymodule: mymodule_init Module loaded successfully.\n");
        return 0;
}

static void __exit mymodule_exit(void){
        pr_info("mymodule: mymodule_exit Module unloaded.\n");
}       

module_init(mymodule_init);
module_exit(mymodule_exit);

