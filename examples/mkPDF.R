
pdf("sample.pdf")
plot(1:10)
abline(v = c(2, 8), col = "red")
abline(h = c(3, 6), col = "blue")
text(5, 5, "Middle")
dev.off()


