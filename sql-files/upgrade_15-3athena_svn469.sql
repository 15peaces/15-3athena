UPDATE `char` SET `uniqueitem_counter` = 0 WHERE `uniqueitem_counter` > 0;
UPDATE `cart_inventory` SET `unique_id` = 0 WHERE `unique_id` > 0;
UPDATE `inventory` SET `unique_id` = 0 WHERE `unique_id` > 0;
UPDATE `mail_attachments` SET `unique_id` = 0 WHERE `unique_id` > 0;
UPDATE `guild_storage` SET `unique_id` = 0 WHERE `unique_id` > 0;
UPDATE `storage` SET `unique_id` = 0 WHERE `unique_id` > 0;
