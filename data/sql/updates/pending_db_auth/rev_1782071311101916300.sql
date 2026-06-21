--
-- WSC-CL: RBAC permissions for the .bgstart / .bgstop battleground commands.
-- Custom (fork) permission ids live in the 1000+ range to avoid colliding with
-- upstream additions. Linked under role 197 ('Gamemaster Commands') so they are
-- granted at the same security level as the other BG control commands.
DELETE FROM `rbac_permissions` WHERE `id` IN (1000, 1001);
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1000, 'Command: bgstart'),
(1001, 'Command: bgstop');

DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` IN (1000, 1001);
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 1000),
(197, 1001);
