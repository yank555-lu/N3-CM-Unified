/*
 * Author: Jean-Pierre Rasquin <yank555.lu@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * CPU freq hard limit - SysFS interface :
 * ---------------------------------------
 *
 * /sys/kernel/cpufreq/hardlimit (rw)
 *
 *   set or show the real hard CPU frequency limit when screen is on
 *
 * /sys/kernel/cpufreq/hardlimit_screen_off (rw)
 *
 *   set or show the real hard CPU frequency limit when screen is off
 *
 * /sys/kernel/cpufreq/wakeup_kick_freq (rw)
 *
 *   set or show the wakeup kick frequency (scaling_min for delay time)
 *
 * /sys/kernel/cpufreq/wakeup_kick_delay (rw)
 *
 *   set or show the wakeup kick duration (in ms)
 *
 * /sys/kernel/cpufreq/touchboost_lo_freq (rw)
 *
 *   set or show touchboost low frequency
 *
 * /sys/kernel/cpufreq/touchboost_hi_freq (rw)
 *
 *   set or show touchboost high frequency
 *
 * /sys/kernel/cpufreq/available_frequencies (ro)
 *
 *   display list of available CPU frequencies for convenience
 *
 * /sys/kernel/cpufreq/current_limit_max (ro)
 *
 *   display current applied hardlimit for CPU max
 *
 * /sys/kernel/cpufreq/current_limit_min (ro)
 *
 *   display current applied hardlimit for CPU min
 *
 * /sys/kernel/cpufreq/version (ro)
 *
 *   display CPU freq hard limit version information
 *
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/cpufreq_hardlimit.h>
#include <linux/cpufreq.h>
#include <linux/powersuspend.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>

unsigned int hardlimit_max_screen_on  = CPUFREQ_HARDLIMIT_MAX_SCREEN_ON_STOCK;  /* default to stock behaviour */
unsigned int hardlimit_max_screen_off = CPUFREQ_HARDLIMIT_MAX_SCREEN_OFF_STOCK; /* default to stock behaviour */
unsigned int wakeup_kick_freq         = CPUFREQ_HARDLIMIT_MIN_STOCK;            /* default to stock behaviour */
unsigned int wakeup_kick_delay        = CPUFREQ_HARDLIMIT_WAKEUP_KICK_DISABLED;
unsigned int wakeup_kick_active       = CPUFREQ_HARDLIMIT_WAKEUP_KICK_INACTIVE;
unsigned int touchboost_lo_freq       = CPUFREQ_HARDLIMIT_TOUCHBOOST_LO_STOCK;  /* default to stock behaviour */
unsigned int touchboost_hi_freq       = CPUFREQ_HARDLIMIT_TOUCHBOOST_HI_STOCK;  /* default to stock behaviour */
unsigned int current_limit_max        = CPUFREQ_HARDLIMIT_MAX_SCREEN_ON_STOCK;
unsigned int current_limit_min        = CPUFREQ_HARDLIMIT_MIN_STOCK;

struct delayed_work stop_wakeup_kick_work;

/* Externally reachable function */

unsigned int check_cpufreq_hardlimit(unsigned int freq)
{
//	#ifdef CPUFREQ_HARDLIMIT_DEBUG
//	pr_info("[HARDLIMIT] check_cpufreq_hardlimit : min = %u / max = %u / freq = %u / result = %u \n",
//			current_limit_min,
//			current_limit_max,
//			freq,
//			max(current_limit_min, min(current_limit_max, freq))
//		);
//	#endif
	return max(current_limit_min, min(current_limit_max, freq));
}

/* Update limits in cpufreq */
void reapply_hard_limits(void)
{
	#ifdef CPUFREQ_HARDLIMIT_DEBUG
	pr_info("[HARDLIMIT] reapply_hard_limits : min = %u / max = %u \n",
			current_limit_min,
			current_limit_max
		);
	#endif
	update_scaling_limits(current_limit_min, current_limit_max);
}

/* Powersuspend */
static void cpufreq_hardlimit_suspend(struct power_suspend * h)
{
	#ifdef CPUFREQ_HARDLIMIT_DEBUG
	pr_info("[HARDLIMIT] suspend : old_min = %u / old_max = %u / new_min = %u / new_max = %u \n",
			current_limit_min,
			current_limit_max,
			CPUFREQ_HARDLIMIT_MIN_STOCK,
			hardlimit_max_screen_off
		);
	#endif
	current_limit_min = CPUFREQ_HARDLIMIT_MIN_STOCK;
	current_limit_max = hardlimit_max_screen_off;
	reapply_hard_limits();
	return;
}

static void cpufreq_hardlimit_resume(struct power_suspend * h)
{
	if(wakeup_kick_delay == CPUFREQ_HARDLIMIT_WAKEUP_KICK_DISABLED) {
		#ifdef CPUFREQ_HARDLIMIT_DEBUG
		pr_info("[HARDLIMIT] resume (no wakeup kick) : old_min = %u / old_max = %u / new_min = %u / new_max = %u \n",
				current_limit_min,
				current_limit_max,
				CPUFREQ_HARDLIMIT_MIN_STOCK,
				hardlimit_max_screen_on
			);
		#endif
		current_limit_min  = CPUFREQ_HARDLIMIT_MIN_STOCK;
		current_limit_max  = hardlimit_max_screen_on;
		wakeup_kick_active = CPUFREQ_HARDLIMIT_WAKEUP_KICK_INACTIVE;
	} else {
		#ifdef CPUFREQ_HARDLIMIT_DEBUG
		pr_info("[HARDLIMIT] resume (with wakeup kick) : old_min = %u / old_max = %u / new_min = %u / new_max = %u \n",
				current_limit_min,
				current_limit_max,
				wakeup_kick_freq,
				max(hardlimit_max_screen_on, min(hardlimit_max_screen_on, wakeup_kick_freq))
			);
		#endif
		current_limit_min  = wakeup_kick_freq;
		current_limit_max  = max(hardlimit_max_screen_on, min(hardlimit_max_screen_on, wakeup_kick_freq));
		wakeup_kick_active = CPUFREQ_HARDLIMIT_WAKEUP_KICK_ACTIVE;
		/* Schedule delayed work to restore stock scaling min after wakeup kick delay */
		schedule_delayed_work(&stop_wakeup_kick_work, usecs_to_jiffies(wakeup_kick_delay * 1000));
	}
	reapply_hard_limits();
	return;
}

static struct power_suspend cpufreq_hardlimit_suspend_data =
{
	.suspend = cpufreq_hardlimit_suspend,
	.resume = cpufreq_hardlimit_resume,
};

/* Delayed work */
static void stop_wakeup_kick(struct work_struct *work)
{
	#ifdef CPUFREQ_HARDLIMIT_DEBUG
	pr_info("[HARDLIMIT] stop wakeup kick : old_min = %u / old_max = %u / new_min = %u / new_max = %u \n",
			current_limit_min,
			current_limit_max,
			CPUFREQ_HARDLIMIT_MIN_STOCK,
			hardlimit_max_screen_on
		);
	#endif

	/* Back to stock scaling min */
	current_limit_min = CPUFREQ_HARDLIMIT_MIN_STOCK;
	current_limit_max = hardlimit_max_screen_on;
	wakeup_kick_active = CPUFREQ_HARDLIMIT_WAKEUP_KICK_INACTIVE;
	reapply_hard_limits();
}

/* sysfs interface for "hardlimit" */
static ssize_t hardlimit_max_screen_on_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hardlimit_max_screen_on);
}

static ssize_t hardlimit_max_screen_on_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_hardlimit, i;

	struct cpufreq_frequency_table *table;

	if (!sscanf(buf, "%du", &new_hardlimit))
		return -EINVAL;

	if (new_hardlimit == hardlimit_max_screen_on)
		return count;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == new_hardlimit) {
			hardlimit_max_screen_on = current_limit_max = new_hardlimit;
			/* Wakeup kick can never be higher than CPU max. hardlimit */
			if(hardlimit_max_screen_on < wakeup_kick_freq)
				wakeup_kick_freq = hardlimit_max_screen_on;
			reapply_hard_limits();
			return count;
		}

	return -EINVAL;

}

static struct kobj_attribute hardlimit_max_screen_on_attribute =
__ATTR(hardlimit, 0666, hardlimit_max_screen_on_show, hardlimit_max_screen_on_store);

/* sysfs interface for "hardlimit_screen_off" */
static ssize_t hardlimit_max_screen_off_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", hardlimit_max_screen_off);
}

static ssize_t hardlimit_max_screen_off_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_hardlimit, i;

	struct cpufreq_frequency_table *table;

	if (!sscanf(buf, "%du", &new_hardlimit))
		return -EINVAL;

	if (new_hardlimit == hardlimit_max_screen_off)
		return count;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == new_hardlimit) {
			hardlimit_max_screen_off = new_hardlimit;
			reapply_hard_limits();
			return count;
		}

	return -EINVAL;

}

static struct kobj_attribute hardlimit_max_screen_off_attribute =
__ATTR(hardlimit_screen_off, 0666, hardlimit_max_screen_off_show, hardlimit_max_screen_off_store);

/* sysfs interface for "wakeup_kick_freq" */
static ssize_t wakeup_kick_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", wakeup_kick_freq);
}

static ssize_t wakeup_kick_freq_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_wakeup_kick_freq, i;

	struct cpufreq_frequency_table *table;

	if (!sscanf(buf, "%du", &new_wakeup_kick_freq))
		return -EINVAL;

	if (new_wakeup_kick_freq == wakeup_kick_freq)
		return count;

	/* Only allow values between current hardlimits */
	if (new_wakeup_kick_freq > hardlimit_max_screen_on || new_wakeup_kick_freq < hardlimit_max_screen_off)
		return -EINVAL;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == new_wakeup_kick_freq) {
			wakeup_kick_freq = new_wakeup_kick_freq;
			/* If we are kicking, update frequencies */
			if(wakeup_kick_active == CPUFREQ_HARDLIMIT_WAKEUP_KICK_ACTIVE) {
				current_limit_min = wakeup_kick_freq;
				reapply_hard_limits();
			}
			return count;
		}

	return -EINVAL;

}

static struct kobj_attribute wakeup_kick_freq_attribute =
__ATTR(wakeup_kick_freq, 0666, wakeup_kick_freq_show, wakeup_kick_freq_store);

/* sysfs interface for "wakeup_kick_delay" */
static ssize_t wakeup_kick_delay_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", wakeup_kick_delay);
}

static ssize_t wakeup_kick_delay_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_wakeup_kick_delay;

	if (!sscanf(buf, "%du", &new_wakeup_kick_delay))
		return -EINVAL;

	if (new_wakeup_kick_delay >= CPUFREQ_HARDLIMIT_WAKEUP_KICK_DISABLED &&
	    new_wakeup_kick_delay <= CPUFREQ_HARDLIMIT_WAKEUP_KICK_DELAY_MAX   ) {

		wakeup_kick_delay = new_wakeup_kick_delay;
		return count;

	}

	return -EINVAL;

}

static struct kobj_attribute wakeup_kick_delay_attribute =
__ATTR(wakeup_kick_delay, 0666, wakeup_kick_delay_show, wakeup_kick_delay_store);

/* sysfs interface for "touchboost_lo_freq" */
static ssize_t touchboost_lo_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", touchboost_lo_freq);
}

static ssize_t touchboost_lo_freq_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_touchboost_lo_freq, i;

	struct cpufreq_frequency_table *table;

	if (!sscanf(buf, "%du", &new_touchboost_lo_freq))
		return -EINVAL;

	if (new_touchboost_lo_freq == touchboost_lo_freq)
		return count;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == new_touchboost_lo_freq) {
			touchboost_lo_freq = new_touchboost_lo_freq;
			/* Touchboost high freq can never be lower than touchboost low freq */
			if(touchboost_hi_freq < touchboost_lo_freq)
				touchboost_hi_freq = touchboost_lo_freq;
			return count;
		}

	return -EINVAL;

}

static struct kobj_attribute touchboost_lo_freq_attribute =
__ATTR(touchboost_lo_freq, 0666, touchboost_lo_freq_show, touchboost_lo_freq_store);

/* sysfs interface for "touchboost_hi_freq" */
static ssize_t touchboost_hi_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", touchboost_hi_freq);
}

static ssize_t touchboost_hi_freq_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

	unsigned int new_touchboost_hi_freq, i;

	struct cpufreq_frequency_table *table;

	if (!sscanf(buf, "%du", &new_touchboost_hi_freq))
		return -EINVAL;

	if (new_touchboost_hi_freq == touchboost_hi_freq)
		return count;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == new_touchboost_hi_freq) {
			touchboost_hi_freq = new_touchboost_hi_freq;
			/* Touchboost low freq can never be higher than touchboost high freq */
			if(touchboost_lo_freq > touchboost_hi_freq)
				touchboost_lo_freq = touchboost_hi_freq;
			return count;
		}

	return -EINVAL;

}

static struct kobj_attribute touchboost_hi_freq_attribute =
__ATTR(touchboost_hi_freq, 0666, touchboost_hi_freq_show, touchboost_hi_freq_store);

/* sysfs interface for "available_frequencies" */
static ssize_t available_frequencies_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned int i;
	ssize_t j = 0;

	struct cpufreq_frequency_table *table;

	table = cpufreq_frequency_get_table(0); /* Get frequency table */

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		j += sprintf(&buf[j], "%d ", table[i].frequency);

	j += sprintf(&buf[j], "\n");
	return j;
}

static struct kobj_attribute available_frequencies_attribute =
__ATTR(available_frequencies, 0444, available_frequencies_show, NULL);

/* sysfs interface for "current_limit_min" */
static ssize_t current_limit_min_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", current_limit_min);
}

static struct kobj_attribute current_limit_min_attribute =
__ATTR(current_limit_min, 0444, current_limit_min_show, NULL);

/* sysfs interface for "current_limit_max" */
static ssize_t current_limit_max_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", current_limit_max);
}

static struct kobj_attribute current_limit_max_attribute =
__ATTR(current_limit_max, 0444, current_limit_max_show, NULL);

/* sysfs interface for "version" */
static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", CPUFREQ_HARDLIMIT_VERSION);
}

static struct kobj_attribute version_attribute =
__ATTR(version, 0444, version_show, NULL);

/* Initialize sysfs folder */
static struct kobject *hardlimit_kobj;

static struct attribute *hardlimit_attrs[] = {
	&hardlimit_max_screen_on_attribute.attr,
	&hardlimit_max_screen_off_attribute.attr,
	&wakeup_kick_freq_attribute.attr,
	&wakeup_kick_delay_attribute.attr,
	&touchboost_lo_freq_attribute.attr,
	&touchboost_hi_freq_attribute.attr,
	&available_frequencies_attribute.attr,
	&current_limit_min_attribute.attr,
	&current_limit_max_attribute.attr,
	&version_attribute.attr,
	NULL,
};

static struct attribute_group hardlimit_attr_group = {
.attrs = hardlimit_attrs,
};

int hardlimit_init(void)
{
	int hardlimit_retval;

        hardlimit_kobj = kobject_create_and_add("cpufreq", kernel_kobj);
        if (!hardlimit_kobj) {
                return -ENOMEM;
        }
        hardlimit_retval = sysfs_create_group(hardlimit_kobj, &hardlimit_attr_group);
        if (hardlimit_retval) {
                kobject_put(hardlimit_kobj);
	} else {
		/* Only register to powersuspend and delayed work if we were able to create the sysfs interface */
		register_power_suspend(&cpufreq_hardlimit_suspend_data);
		INIT_DELAYED_WORK_DEFERRABLE(&stop_wakeup_kick_work, stop_wakeup_kick);
	}

        return (hardlimit_retval);
}
/* end sysfs interface */

void hardlimit_exit(void)
{
	unregister_power_suspend(&cpufreq_hardlimit_suspend_data);
	kobject_put(hardlimit_kobj);
}

module_init(hardlimit_init);
module_exit(hardlimit_exit);
