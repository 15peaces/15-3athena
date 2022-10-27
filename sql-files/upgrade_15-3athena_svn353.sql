ALTER TABLE `auction` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
ALTER TABLE `cart_inventory` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
ALTER TABLE `char` ADD COLUMN `uniqueitem_counter` bigint(20) NOT NULL AFTER `title_id`;
ALTER TABLE `guild_storage` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
ALTER TABLE `inventory` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
ALTER TABLE `mail_attachments` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
ALTER TABLE `storage` ADD `unique_id` BIGINT NOT NULL DEFAULT '0';
